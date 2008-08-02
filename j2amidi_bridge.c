/* jackmidi2alsaseq.c
 *
 * copies MIDI events from a JACK MIDI client to an ALSA sequencer client
 *
 * Copyright (C) 2005 Lars Luthman, based on alsaseq2jackmidi.c by Sean Bolton.
 * Copyright (c) 2008 Nedko Arnaudov <nedko@arnaudov.name>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307, USA.
 */

/* compile with LASH: 

gcc -ansi -pedantic -O2 -Wall -o jackmidi2alsaseq jackmidi2alsaseq.c `pkg-config --cflags --libs jack alsa lash-1.0`

or without LASH:

gcc -ansi -pedantic -O2 -Wall -o jackmidi2alsaseq jackmidi2alsaseq.c `pkg-config --cflags --libs jack alsa` -DNO_LASH

*/


#include <sched.h>
#include <signal.h>
#if !defined(__USE_BSD)
#define __USE_BSD /* to get snprintf() */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#if !defined(__USE_POSIX199309)
#define __USE_POSIX199309
#endif
#include <time.h>
#include <unistd.h>

#include <alsa/asoundlib.h>
#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/ringbuffer.h>


jack_client_t *jack_client;
jack_port_t   *jack_midi_port;
int keep_running = 1;
unsigned long int ringbuffer_overflows = 0;
snd_seq_t *seq_handle;
snd_midi_event_t *alsa_encoder;
jack_ringbuffer_t *jack_ringbuffer;
char* jack_name = NULL;
int portid;
int queue_id;


int init_alsa(const char * client_name);
int init_jack(const char * client_name);
void sigint_handler(int i);
int jack_callback(jack_nframes_t nframes, void *arg);
void output_event();


int main(int argc, char **argv) {

  unsigned long old_ringbuffer_overflows = 0;
  struct timespec sleep_time = { 0, 1e6 };
  const char * client_name;

  if (argc == 2)
  {
    client_name = argv[1];
  }
  else
  {
    client_name = "j2a_bridge";
  }
  
  /* Initialise connections and signal handlers */
  if (!(init_alsa(client_name) && init_jack(client_name)))
    exit(1);
  signal(SIGINT, &sigint_handler);
  signal(SIGTERM, &sigint_handler);
  
  /* Loop until we get a SIGINT or SIGTERM */
  while (keep_running) {
    
    /* Report overflows */
    if (old_ringbuffer_overflows != ringbuffer_overflows) {
      fprintf(stderr, "Overflow, MIDI events are coming in too fast!\n");
      old_ringbuffer_overflows = ringbuffer_overflows;
    }
    
    /* Write MIDI events to the ALSA sequencer port */
    while (jack_ringbuffer_read_space(jack_ringbuffer) >= sizeof(size_t) && 
	   keep_running) {
      output_event();
    }
    
    nanosleep(&sleep_time, NULL);
  }
  
  /* Clean up */
  jack_client_close(jack_client);
  jack_ringbuffer_free(jack_ringbuffer);
  snd_seq_close(seq_handle);
  
  return 0;
}


int init_alsa(const char * client_name) {
  
  /* Get a sequencer handle */
  if (snd_seq_open(&seq_handle, "hw", SND_SEQ_OPEN_OUTPUT, 0) < 0) {
    fprintf(stderr, "Error opening ALSA sequencer.\n");
    return 0;
  }
  snd_seq_set_client_name(seq_handle, client_name);
  
  /* Create an output port */
  if ((portid = snd_seq_create_simple_port(seq_handle, "capture",
					   SND_SEQ_PORT_CAP_READ |
					   SND_SEQ_PORT_CAP_SUBS_READ,
					   SND_SEQ_PORT_TYPE_HARDWARE)) < 0){
    fprintf(stderr, "Error creating sequencer port.\n");
    return 0;
  }
  
  /* Initialise miscellaneous other stuff */
  queue_id = snd_seq_alloc_queue(seq_handle);
  snd_midi_event_new(1024, &alsa_encoder);
  snd_seq_start_queue(seq_handle, queue_id, NULL); 
  
  return 1;
}


int init_jack(const char * client_name) {
  jack_status_t status;
  
  /* Create a JACK client */
  jack_client = jack_client_open(client_name, 0, &status);
  if (jack_client == 0) {
    fprintf(stderr, "Failed to connect to JACK server!\n");
    return 0;
  }
  
  /* Create a MIDI input port */
  jack_midi_port = jack_port_register(jack_client, "playback",
				      JACK_DEFAULT_MIDI_TYPE,
				      JackPortIsInput, 0);
  if (!jack_midi_port) {
    fprintf(stderr, "Failed to create JACK MIDI port!\n");
    return 0;
  }
  
  /* Initialise the ringbuffer */
  jack_ringbuffer = jack_ringbuffer_create(2048);
  if (!jack_ringbuffer) {
    fprintf(stderr, "Failed to create ringbuffer!\n");
    return 0;
  }
  jack_ringbuffer_reset(jack_ringbuffer);
  
  /* Set process callback function and activate */
  jack_set_process_callback(jack_client, jack_callback, jack_ringbuffer);
  if (jack_activate(jack_client)) {
    fprintf(stderr, "Failed to activate JACK client!\n");
    return 0;
  }

  return 1;
}


/* This is just so we can clean up if the user presses Ctrl-C in the shell */
void sigint_handler(int i) {
  keep_running = 0;
}


int jack_callback(jack_nframes_t nframes, void *arg) {
  jack_ringbuffer_t* jack_ringbuffer = arg;
  void* midi_port_buf = jack_port_get_buffer(jack_midi_port, nframes);
  jack_midi_event_t jack_midi_event;
  jack_nframes_t jack_midi_event_index = 0;
  jack_nframes_t jack_midi_event_count = jack_midi_get_event_count(midi_port_buf);
  
  /* Loop while there are MIDI events in the input buffer */
  while (jack_midi_event_index < jack_midi_event_count) {
    jack_midi_event_get(&jack_midi_event, midi_port_buf, 
			jack_midi_event_index);
    jack_midi_event_index++;
    
    /* Check if we have enough space in the ringbuffer for the event */
    if (jack_ringbuffer_write_space(jack_ringbuffer) < 
	jack_midi_event.size + sizeof(size_t)) {
      ++ringbuffer_overflows;
    }
    
    /* Write the event to the ringbuffer */
    else {
      jack_ringbuffer_write(jack_ringbuffer, 
			    (char*)&jack_midi_event.size, 
			    sizeof(size_t));
      jack_ringbuffer_write(jack_ringbuffer, 
			    (char*)jack_midi_event.buffer,
			    jack_midi_event.size);
    }
  }
  
  return 0;
}

	
void output_event() {
  size_t event_size;
  static char event_buffer[1024];
  snd_seq_event_t alsa_event;
  static struct timespec sleep_time = { 0, 1e4 };
  
  /* Read the size of the MIDI data and wait until we have that much
     data to read on the ringbuffer */
  jack_ringbuffer_read(jack_ringbuffer, (char*)&event_size, sizeof(size_t));
  while (jack_ringbuffer_read_space(jack_ringbuffer) < event_size &&
	 keep_running)
    nanosleep(&sleep_time, NULL);
  
  /* Read the MIDI data and make an ALSA MIDI event from it */
  jack_ringbuffer_read(jack_ringbuffer, event_buffer, event_size);
  snd_seq_ev_clear(&alsa_event);
  if (snd_midi_event_encode(alsa_encoder, (unsigned char*)event_buffer, 
			    event_size, &alsa_event)) {
    snd_seq_ev_set_source(&alsa_event, portid);
    snd_seq_ev_set_subs(&alsa_event);
    snd_seq_ev_schedule_tick(&alsa_event, queue_id, 1, 0);
    snd_seq_event_output_direct(seq_handle, &alsa_event);
  }
}
