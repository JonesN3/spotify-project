#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>

#include "audio.h"
#include "queue.h"
//#include <libspotify/api.h>
#include </uio/hume/student-u56/espenaj/local/include/libspotify/api.h>

extern const uint8_t g_appkey[];
extern const size_t g_appkey_size;
extern const char *username;
extern const char *password;

static audio_fifo_t g_audiofifo;
static int globPlaying;
static void on_end_of_track(sp_session *session);

#define DEBUG 1
int g_logged_in;

void debug(const char *format, ...)
{
   if (!DEBUG)
      return;

   va_list argptr;
   va_start(argptr, format);
   vprintf(format, argptr);
   printf("\n");
}

void play(sp_session *session, sp_track *track)
{
    sp_error error = sp_session_player_load(session, track);
    if (error != SP_ERROR_OK) {
        fprintf(stderr, "Error: %s\n", sp_error_message(error));
        exit(1);
    }

    int max_width = 30;

    sp_artist *artist = sp_track_artist(track, 0);
    sp_album *album = sp_track_album(track);
    int duration = sp_track_duration(track);
 
    printf("Plying track\n");
    printf("\n");
    printf("%s\n", sp_artist_name(artist));
    printf("%s\n", sp_track_name(track));
    printf("%s\n", sp_album_name(album));
    printf("[%d:%d]\n", duration/60000, (duration/1000) % 60);
    printf("\n\n");
    sp_session_player_play(session, 1);

    char command;
    char commandStr[100];

    fputs("> ", stdout);
    //command = fgetc(stdin);
    //fgets(commandStr, sizeof(commandStr), stdin);
    //printf("command: %c\n", command);

    if(command == 'q') { 
	printf("abort play\n");	
	//sp_session_player_play(session, 0);
	on_end_of_track(session);
    }
}

static void on_search_complete(sp_search *search, void *userdata)
{
    debug("callback: on_search_complete");
    sp_error error = sp_search_error(search);
    if (error != SP_ERROR_OK) {
        fprintf(stderr, "Error: %s\n", sp_error_message(error));
        exit(1);
    }
 
    int num_tracks = sp_search_num_tracks(search);
    if (num_tracks == 0) {
        printf("\nSorry, couldn't find that track.\n");
	globPlaying = 0;
	handler((sp_session*)userdata);
        exit(0);
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
   debug("callback: on_end_of_track");
   audio_fifo_flush(&g_audiofifo);
    sp_session_player_play(session, 0);
    sp_session_player_unload(session);
   globPlaying = 0;
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

int logIn(void)
{
   sp_error error;
   sp_session *session;
   
   // create the spotify session
   spconfig.application_key_size = g_appkey_size;
   error = sp_session_create(&spconfig, &session);
   if (error != SP_ERROR_OK) {
      fprintf(stderr, "Error: unable to create spotify session: %s\n", sp_error_message(error));
      return 1;
   }

   int next_timeout = 0;

   g_logged_in = 0;
   sp_session_login(session, username, password, 0, NULL);

   handler(session);
   printf("success!\n");
   return 0;
}

int handler(sp_session *session)
{
   int next_timeout = 0;
   int selection;
   globPlaying = 0;
   selection = menu();
   if(selection == 0)
   {
       printf("Exiting\n");
       exit(0);
   }
    while(1) {
	sp_session_process_events(session, &next_timeout);

	if(!globPlaying)
	{
	    if(selection == 1)
	    { 
		run_search(session);
		globPlaying = 1;
	    }else if(selection == 2) {
		printf("playlist play");
	    } else if(selection == 3) {
    	
	    } else {
		printf("\nerror: illegal menu choice\n");
		handler(session);
	    }
	}
    }
}

int menu(void)
{
    int selected;
    printf("\n--Menu--\n");
    printf("0: exit\n");
    printf("1: search\n");
    printf("2: play playlist (not implemented) \n");
    printf("3: list playlists (not implemented) \n");
    char input[100];
    fputs("> ", stdout);
    fgets(input, sizeof(input) - 1, stdin);
    sscanf(input, "%d", &selected);
    printf("selected: %d\n", selected);

    return selected;
}

int main(void)
{
   printf("hello spotify!\n");
   printf("username: %s\n", username);
   audio_init(&g_audiofifo);
   logIn();
   return 0;
}
