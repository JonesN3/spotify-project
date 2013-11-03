#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>

#include "audio.h"
#include "queue.h"
#include <libspotify/api.h>
#include </usr/local/include/libspotify/api.h>

extern const uint8_t g_appkey[];
extern const size_t g_appkey_size;
extern const char *username;
extern const char *password;

static audio_fifo_t g_audiofifo;


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
 
    printf("\n");
    printf("Playing...\n");
 
    sp_session_player_play(session, 1);
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
        printf("Sorry, couldn't find that track. =/\n");
        exit(0);
    }
 
    printf("Found track!\n");
    sp_track *track = sp_search_track(search, 0);
    play((sp_session*)userdata, track);
}

void run_search(sp_session *session)
{
    // ask the user for an artist and track
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
   run_search(session);
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
   printf("Done. \n");
   exit(0);
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

   //this is a bad way of doing the loop
   while (1) {
      sp_session_process_events(session, &next_timeout);
      //usleep(next_timeout * 10);
  }

   printf("success!\n");
   return 0;
}

int main(void)
{
   printf("hello spotify!\n");
   printf("username: %s\n", username);
   audio_init(&g_audiofifo);
   logIn();
   return 0;
}

