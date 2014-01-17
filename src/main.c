#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <alloca.h> //??
#include <assert.h>
#include <signal.h>
#include <sys/ioctl.h>

#include "audio.h"
#include "queue.h"
#include <libspotify/api.h>
//#include </uio/hume/student-u56/espenaj/local/include/libspotify/api.h>

extern const uint8_t g_appkey[];
extern const size_t g_appkey_size;
extern const char *usernameExt;
extern const char *passwordExt;

char *username;
char *password;

static audio_fifo_t g_audiofifo;
static int globPlaying;
static void on_end_of_track(sp_session *session);
void quit();
void endPlayer();

#define DEBUG 1
int g_logged_in;
sp_session *g_session;
sp_playlist *g_selectedList;
int g_trackIndex;
int g_menuChoice;
int g_shuffleMode;
int g_shuffleMode2;
int *g_shuffleArray;
int g_playbackOn;
pid_t pid;
sp_playlist **g_playlistArray;

void debug(const char *format, ...)
{
   if (!DEBUG)
      return;

   va_list argptr;
   va_start(argptr, format);
   vprintf(format, argptr);
   printf("\n");
}

void playShell()
{
   char input [1000];
   printf("play shell"); 

   while(g_playbackOn) {
      printf("%d\n", g_playbackOn);
      fputs("\n>", stdout);
      fgets(input, sizeof(input), stdin);

      if(strcmp(input, "exit\n") == 0) quit();
      if(strcmp(input, "next\n") == 0) playlistGoNext();
      if(strcmp(input, "stop\n") == 0) endPlayer();
      if(strcmp(input, "shell\n") == 0) handler(g_session);
   }

   return;
}

void printPlayInfo(sp_track *track)
{

   sp_artist *artist = sp_track_artist(track, 0);
   sp_artist *artist2 = sp_track_artist(track, 1);
   sp_album *album = sp_track_album(track);
   int duration = sp_track_duration(track);
   int allArtistsW, playlistW;
   const char *artist2Name;
   const char *playlistName;
   char *allArtistName;
   char *columnsVar = getenv("COLUMNS");


   const char *trackName = sp_track_name(track);
   const char *artistName = sp_artist_name(artist);
   if(artist2 !=NULL) artist2Name = sp_artist_name(artist2);
   const char *albumName = sp_album_name(album);
   if(g_selectedList != NULL) playlistName = sp_playlist_name(g_selectedList);

   if(artist2 != NULL) {
      allArtistName = malloc(strlen(artistName) + strlen(artist2Name) + 3);
      strcpy(allArtistName, artistName);
      strcat(allArtistName, ", ");
      strcat(allArtistName, artist2Name);
   }

   if(columnsVar != NULL) {

      int columns = strtol(getenv("COLUMNS"), NULL, 10);
      int trackW = strlen(trackName) + (columns - strlen(trackName)) / 2;
      int artistW = strlen(artistName) + (columns - strlen(artistName)) / 2;
      if(artist2 != NULL) allArtistsW = strlen(allArtistName) + (columns - strlen(allArtistName)) / 2;
      int albumW = strlen(albumName) + (columns - strlen(albumName)) / 2;
      int headW = strlen("-Playing-") + (columns - strlen("-Playing-")) / 2;
      if(g_selectedList != NULL)  playlistW = strlen(playlistName) + (columns - strlen(playlistName)) / 2;

      printf("\n\n");
      printf("%*s\n", headW, "-Playing-");
      if(g_selectedList != NULL) printf("%*s\n", playlistW, playlistName);
      printf("\n");
      if(artist2 != NULL) printf("%*s\n", allArtistsW, allArtistName);
      else printf("%*s\n", artistW, artistName);
      printf("%*s\n",trackW, trackName);
      printf("%*s\n", albumW, albumName);
   }
}

void playProgress(sp_track *track)
{
   //just for fun, doesnt really work, but can give an estiamte
   int i;
   int duration = sp_track_duration(track);
   int columns = strtol(getenv("COLUMNS"), NULL, 10);
   int pos;
   char playtime [50];
   duration = duration / 1000;
   int dSecs, dMins, iSecs, iMins;
   dMins = iMins = 0;
   iSecs = 1;

   dSecs = duration;

   while(dSecs >= 60) {
      dSecs = dSecs - 60;
      dMins++;
   }


   for(i = 1; i <= duration; i++) {
      fflush(stdout);
      usleep(1000000);
      if(g_playbackOn != 1) return;
      iSecs++;
      while(iSecs >= 60) {
	 iSecs = iSecs - 60;
	 iMins++; 
      }
      sprintf(playtime, "[%02d:%02d | %02d:%02d]\r", iMins, iSecs, dMins, dSecs);
      pos = strlen(playtime) + (columns - strlen(playtime)+1) / 2;
      printf("%*s\r", pos, playtime);
      fflush(stdout);
   }
   printf("\n");
   return;
}

void play(sp_session *session, sp_track *track)
{

   g_playbackOn = 1;

   sp_error error = sp_session_player_load(session, track);
   if (error != SP_ERROR_OK) {
      fprintf(stderr, "\nError: %s\n", sp_error_message(error));
      ////printf("track: %s, artist: %s\n", sp_track_name(track), sp_artist_name(sp_track_artist(track, 0)));
      sp_session_player_unload(session);
      if (g_selectedList != NULL){ playlistGoNext(); printf("playing next song...\n\n");}
      return;
   }

   globPlaying = 1;
   sp_session_player_play(session, 1);
   printPlayInfo(track);
   playProgress(track);
}

static void on_search_complete(sp_search *search, void *userdata)
{
   g_menuChoice = -1;
   debug("callback: on_search_complete");
   sp_error error = sp_search_error(search);
   if (error != SP_ERROR_OK) {
      fprintf(stderr, "Error: %s\n", sp_error_message(error));
      return;
   }

   int num_tracks = sp_search_num_tracks(search);
   if (num_tracks == 0) {
      printf("\nSorry, couldn't find that track.\n");
      globPlaying = 0;
      return;
   }

   printf("Found track!\n");
   sp_track *track = sp_search_track(search, 0);
   play((sp_session*)userdata, track);
}

void run_search(sp_session *session)
{
   // ask the user for an artist and track
   printf("\n--Search and play--\n");
   char artist[1024];
   printf("Artist: ");
   fgets(artist, 1024, stdin);
   artist[strlen(artist)-1] = '\0';

   char track[1024];
   printf("Track: ");
   fgets(track, 1024, stdin);
   track[strlen(track)-1] = '\0';

   // format the query, e.g. "artist:<artist> track:<track>"
   char q[4096];
   sprintf(q, "artist:\"%s\" track:\"%s\"", artist, track);

   // start the search
   sp_search_create(session, q, 0, 1, 0, 0, 0, 0, 0, 0, SP_SEARCH_STANDARD,
	 &on_search_complete, session);
}


static void on_login(sp_session *session, sp_error error)
{
   debug("callback: on_login");
   if (error != SP_ERROR_OK) {
      fprintf(stderr, "Error: unable to log in: %s\n", sp_error_message(error));
      exit(1);
   }

   g_logged_in = 1;
   //run_search(session);
}

static int on_music_delivered(sp_session *session, const sp_audioformat *format, const void *frames, int num_frames)
{
   audio_fifo_t *af = &g_audiofifo;
   audio_fifo_data_t *afd;
   size_t s;

   if (num_frames == 0)
      return 0; // Audio discontinuity, do nothing

   pthread_mutex_lock(&af->mutex);

   /* Buffer one second of audio */
   if (af->qlen > format->sample_rate) {
      pthread_mutex_unlock(&af->mutex);

      return 0;
   }

   s = num_frames * sizeof(int16_t) * format->channels;

   afd = malloc(sizeof(*afd) + s);
   memcpy(afd->samples, frames, s);

   afd->nsamples = num_frames;

   afd->rate = format->sample_rate;
   afd->channels = format->channels;

   TAILQ_INSERT_TAIL(&af->q, afd, link);
   af->qlen += num_frames;

   pthread_cond_signal(&af->cond);
   pthread_mutex_unlock(&af->mutex);

   return num_frames;
}

static void on_main_thread_notified(sp_session *session)
{
   //debug("callback: on_main_thread_notified");
}


static void on_log(sp_session *session, const char *data)
{
   // this method is *very* verbose, so this data should really be written out to a log file
}

static void on_end_of_track(sp_session *session)
{
   debug("callback: on_end_of_track\n");
   audio_fifo_flush(&g_audiofifo);
   sp_session_player_play(session, 0);
   g_trackIndex++;
   globPlaying = 0;
   g_playbackOn = 0;
   printf("END OF TRACK : %d", g_playbackOn);
   //handler(session); //is this right ?
}

static sp_session_callbacks session_callbacks = {
   .logged_in = &on_login,
   .notify_main_thread = &on_main_thread_notified,
   .music_delivery = &on_music_delivered,
   .log_message = &on_log,
   .end_of_track = &on_end_of_track
};

static sp_session_config spconfig = {
   .api_version = SPOTIFY_API_VERSION,
   .cache_location = "tmp",
   .settings_location = "tmp",
   .application_key = g_appkey,
   .application_key_size = 0, // set in main()
   .user_agent = "spot",
   .callbacks = &session_callbacks,
   NULL
};

int printPlaylists(sp_session *g_session) 
{
   printf("\n-- Print playlists --\n");
   sp_playlistcontainer *pc = sp_session_playlistcontainer(g_session);
   sp_playlist *playlist;
   int i; //for the for loop;

   for(i = 0; i < sp_playlistcontainer_num_playlists(pc); ++i) {
      switch (sp_playlistcontainer_playlist_type(pc, i)) {
	 case SP_PLAYLIST_TYPE_PLAYLIST: 
	    playlist = sp_playlistcontainer_playlist(pc, i);
	    printf("%d. %s\n", i, sp_playlist_name(playlist));
	    break;
	 default:
	    break;
      }
   }

   g_menuChoice = -1;
   return 0;
}

int testPlaylistPlay(sp_session *g_session, int playlistIndex)
{
   printf("\ntesting playlist play");
   sp_playlistcontainer *pc = sp_session_playlistcontainer(g_session);
   sp_playlist *playlist;
   playlist = sp_playlistcontainer_playlist(pc, playlistIndex);

   if(!sp_playlist_is_loaded(playlist)){
      printf("playlist is not loaded\n");
      return 0;
   }
   if(sp_playlist_is_loaded(playlist)) {
      printf("\nplaylist is loaded\n");
   }
   printf("\n-- Playing first song in playlist: %s, nr: %d\n --", sp_playlist_name(playlist), playlistIndex);
   sp_track *track = sp_playlist_track(playlist, 0);
   play(g_session, track);
   g_menuChoice = -1;
   return 0;

}

void loadPlaylist(int index) 
{
   printf("load playlist\n");
   sp_playlistcontainer *pc = sp_session_playlistcontainer(g_session);
   g_selectedList = sp_playlistcontainer_playlist(pc, index);
}

void shuffleList(void) {
   printf("errry i'm shuffeling\n");
   sp_track *shuffled [sp_playlist_num_tracks(g_selectedList)];
   sp_track *tmp;

   int length = sizeof(g_playlistArray) / sizeof(g_playlistArray[0]);
   printf("length: %d\n", length);

   int random;
   int i;
   for(i = sizeof(g_playlistArray); i > 0; --i) {
      random = rand() % length;
      tmp = g_playlistArray[i];
      g_playlistArray[i] = g_playlistArray[random];
      g_playlistArray[random] = tmp;
   }
}


void shuffleNumber(void) {
   if(!g_shuffleMode2) loadPlaylist(4);
   //empty number array
   int size = sp_playlist_num_tracks(g_selectedList);
   if(g_selectedList != NULL) printf("%s", sp_playlist_name(g_selectedList));
   printf("size: %d\n", size);
   int i = 0, random, tmp;
   int ascending[size];
   int shuffled[size];
   for(i = 0; i < size; ++i) {
      ascending[i] = i; 
      printf("ascending[%d] = %d \n",i, ascending[i]);
   }

   //shuffle the numbers
   for(i = size-1; i > 0; i--) {
      random = rand() % size;
      printf("random = %d\n", random);
      tmp = ascending[i];
      printf(" tmp: %d \n ascending[%d]: %d ", tmp, i, ascending[i]);
      ascending[i] = ascending[random];
      ascending[random] = tmp;
   }

   for(i = 0; i < size; i++) {
      printf("shuffled[%d] = %d\n",i, ascending[i]);
   }
   if(!g_shuffleMode2) g_menuChoice = -1;
   g_shuffleArray = ascending;
   printf("g_shuffleArray: %d", g_shuffleArray[0]);

   printf("done shuffleing\n");
}

sp_track ** createPlaylistArray() {
   int i;   
   sp_track *plTracks[sp_playlist_num_tracks(g_selectedList)];

   for(i = 0; i < sp_playlist_num_tracks(g_selectedList); ++i) {
      plTracks[i] = sp_playlist_track(g_selectedList, i); 
   }

   return plTracks;
}

void playthatlist() {
   printf("Play that playlist!\n");
   int *shuffledArray = NULL;
   int index;
   char input [1000];

   if(g_selectedList == NULL) {
      fputs("Playlist number: ", stdout);
      fgets(input, sizeof(input) - 1, stdin);
      sscanf(input, "%d", &index);
      loadPlaylist(index);
      g_trackIndex = 0;
      g_playlistArray = createPlaylistArray();
      if(g_shuffleMode) shuffleList();
      if(g_shuffleMode2) {
	 shuffleNumber();
	 printf("SN: %d\n", g_shuffleArray[0]);
      }
   }

   if(sp_playlist_num_tracks(g_selectedList)-1 < g_trackIndex) {
      printf("no more tracks in playlist\n");
      endPlayer();
      return;
   }
   if(g_shuffleMode2) 
      play(g_session, sp_playlist_track(g_selectedList, g_shuffleArray[g_trackIndex]));
   else 
      play(g_session, sp_playlist_track(g_selectedList, g_trackIndex));
   //play(g_session, sp_playlist_track(g_selectedList, g_trackIndex));
   //play(g_session, g_playlistArray[g_trackIndex]);
   //++g_trackIndex;
}




void playlistGoNext() {
   ++g_trackIndex;
   if(sp_playlist_num_tracks(g_selectedList) -1 < g_trackIndex) {
      printf("end of list!\n");
      endPlayer();
      return;
   }
   if(g_shuffleMode2) play(g_session, sp_playlist_track(g_selectedList, g_shuffleArray[g_trackIndex]));
   else play(g_session, sp_playlist_track(g_selectedList, g_trackIndex));
   //play(g_session, g_playlistArray[g_trackIndex]);
}

void endPlayer() {
   printf("end player\n");
   g_selectedList = NULL;
   g_playlistArray = NULL;
   g_trackIndex = 0;
   g_shuffleMode = 0;
   g_menuChoice = -1;
   on_end_of_track(g_session);
}


void listSongsInPlaylist(sp_session *session, int index)
{

   sp_playlistcontainer *pc = sp_session_playlistcontainer(session);
   sp_playlist *playlist;
   sp_track *track;
   int i;

   printf("test\n");
   playlist = sp_playlistcontainer_playlist(pc, index);
   printf("\n--Listing songs in playlist: %s--", sp_playlist_name(playlist));

   for(i = 0; i < sp_playlist_num_tracks(playlist); ++i)
   {
      track = sp_playlist_track(playlist, i);
      printf("\n%d. %s", i, sp_track_name(track));
   }

   g_menuChoice = -1;
}

int logIn(void)
{
   sp_error error;
   //sp_session *g_session;

   // create the spotify session
   spconfig.application_key_size = g_appkey_size;
   error = sp_session_create(&spconfig, &g_session);
   if (error != SP_ERROR_OK) {
      fprintf(stderr, "Error: unable to create spotify session: %s\n", sp_error_message(error));
      return 1;
   }

   g_logged_in = 0;
   sp_session_login(g_session, username, password, 0, NULL);

   printf("logged in.\n");
   handler(g_session);
   return 0;
}

void quit(void)
{
   printf("Exiting\n");
   sp_session_logout(g_session);
   exit(0);
}


int handler(sp_session *session)
{
   printf("\nHandler \n");
   int next_timeout = 0;
   int index;
   int selection = 0;
   char input[100];

   globPlaying = 0;
   selection = g_menuChoice;
   if(g_menuChoice == -1 || g_menuChoice == 9) selection = menu();
   g_menuChoice = selection;
   if(selection == 0) quit();

   while(1) {
      sp_session_process_events(session, &next_timeout);

      if(!globPlaying)
      {
	 if(selection == -1) handler(session);
	 if(g_menuChoice == -1 || g_menuChoice == 9) handler(session);

	 if(selection == 1)
	 { 
	    run_search(session);
	    globPlaying = 1;
	 }else if(selection == 2) {
	    printf("playlist play test");
	    testPlaylistPlay(session, 0);
	 } else if(selection == 3) {
	    printPlaylists(g_session);
	 } else if(selection == 4) {
	    fputs("Playlist number: ", stdout);
	    fgets(input, sizeof(input) - 1, stdin);
	    sscanf(input, "%d", &index);
	    listSongsInPlaylist(session, index);
	 } else if(selection == 5) {
	    playthatlist();
	 } else if(selection == 6) {
	    g_shuffleMode = 1;
	    playthatlist();
	 } else if(selection == 7) {
	    printf("test shuffle");
	    shuffleNumber();
	 } else if(selection == 8) {
	    printf("test shuffle2");
	    g_shuffleMode2 = 1;
	    playthatlist();
	 } else {
	    printf("\nerror: illegal menu choice\n");
	    g_menuChoice = 9;
	 }
      } else {
	 //usleep(4000);
	 //if(g_playbackOn == 1) playShell();
	 while(g_playbackOn == 1) {


	 }
      }
   }
}

int menu(void)
{
   int selected;
   if(g_menuChoice == 9) {
      printf("\n\n--Menu--\n");
      printf("0: exit\n");
      printf("1: search\n");
      printf("2: test playlist-play \n");
      printf("3: list playlists\n");
      printf("4: list songs in playlist [n]\n");
      printf("5: play playlist [n]\n");
      printf("9: help\n");
   }
   char input[100];
   fputs("> ", stdout);
   fgets(input, sizeof(input) - 1, stdin);
   sscanf(input, "%d", &selected);
   printf("selected: %d\n", selected);

   return selected;
}

void getUserInfo(void)
{
    char *inputU = malloc(sizeof(char)*30);
    char *inputP = malloc(sizeof(char)*30);
    fgets(inputU, 30, stdin);
    fgets(inputP, 30, stdin);

    username = strtok(inputU, "\n");
    password = strtok(inputP, "\n");
}

int main(void)
{
    username = malloc(sizeof(char)*30);
    password = malloc(sizeof(char)*30);
    //username = "nil\n";
    //password = "nil\n";
   printf("hello spotify!\n");
   printf("username: %s\n", username);
    getUserInfo();
   printf("username: %s\n", username);
   printf("password: %s\n", password);
   audio_init(&g_audiofifo);
   g_menuChoice = 9;
   g_playbackOn = 0;
   logIn();
   return 0;
}

