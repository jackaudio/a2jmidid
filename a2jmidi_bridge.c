/* alsaseq2jackmidi.c
 *
 * copies MIDI events from an ALSA sequencer client to a JACK MIDI client
 *
 * Copyright (c)2005 Sean Bolton.
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

/* compile with: gcc -Wall -o alsaseq2jackmidi alsaseq2jackmidi.c -ljack -lasound */

/* gcc -Wall -I/tmp/jackmidi/include -o alsaseq2jackmidi alsaseq2jackmidi.c -L/tmp/jackmidi/lib -ljack -lasound */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <alsa/asoundlib.h>
#include <jack/jack.h>
#include <jack/midiport.h>
#include <jack/ringbuffer.h>

snd_seq_t *seq_handle;

snd_midi_event_t *alsa_decoder;

jack_ringbuffer_t *jack_ringbuffer;

jack_client_t *jack_client;
jack_port_t   *jack_midi_port;

int jack_write_overflows = 0,
    old_jack_write_overflows = 0,
    events_copied = 0,
    old_events_copied = 0;

bool g_keep_walking;

void
midi_action(snd_seq_t *seq_handle)
{
    snd_seq_event_t *ev;
    static unsigned char buffer[16];
    long count;

    do {
        snd_seq_event_input(seq_handle, &ev);

        count = snd_midi_event_decode(alsa_decoder, buffer + 1, 16, ev);
        if (count > 0 && count < 16) {
            buffer[0] = (unsigned char)count;
            count++;
            if (jack_ringbuffer_write(jack_ringbuffer, (char *)buffer, count) != count) {
                fprintf(stderr, "ringbuffer overflow!\n");
            }
        }

        snd_seq_free_event(ev);
    } while (snd_seq_event_input_pending(seq_handle, 0) > 0);
}

int
jack_callback(jack_nframes_t nframes, void *arg)
{
    static unsigned char buffer[16];
    size_t count;
    unsigned char *p;
    void* port_buf = jack_port_get_buffer(jack_midi_port, nframes);

    jack_midi_clear_buffer(port_buf);

    while (jack_ringbuffer_read_space(jack_ringbuffer)) {
        count  = jack_ringbuffer_peek(jack_ringbuffer, (char*)buffer, 1);
        if (count) {
            count = (size_t)buffer[0];
            if (jack_ringbuffer_read(jack_ringbuffer, (char*)buffer, count + 1) != count + 1) {
                fprintf(stderr, "???? short read from ringbuffer!\n"); /* shouldn't happen */
            } else {
	      /* -FIX- this should have the frame time of the event, instead of '0': */
	        p = jack_midi_event_reserve(port_buf, 0, count);
                if (p) {
                    memcpy(p, buffer + 1, count);
                    events_copied++;
                } else
                    jack_write_overflows++;
            }
        }
    }

    return 0;
}

static
void
jack_shutdown(
  void * arg)
{
  fprintf(stderr, "JACK shutdown notification received.\n");
  g_keep_walking = false;
}

static
void
sigint_handler(
  int i)
{
  g_keep_walking = false;
}

int
main(int argc, char *argv[])
{
    int portid;
    int npfd;
    struct pollfd *pfd;
    const char * client_name;
    jack_status_t status;

    if (argc == 2)
    {
      client_name = argv[1];
    }
    else
    {
      client_name = "a2j_bridge";
    }

    /* Create ALSA sequencer client */
    if (snd_seq_open(&seq_handle, "hw", SND_SEQ_OPEN_INPUT, 0) < 0) {
        fprintf(stderr, "Error opening ALSA sequencer.\n");
        exit(1);
    }
    snd_seq_set_client_name(seq_handle, client_name);
    if ((portid = snd_seq_create_simple_port(seq_handle, "playback",
            SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE,
            SND_SEQ_PORT_TYPE_APPLICATION)) < 0) {
        fprintf(stderr, "Error creating sequencer port.\n");
        exit(1);
    }
    npfd = snd_seq_poll_descriptors_count(seq_handle, POLLIN);
    pfd = (struct pollfd *)alloca(npfd * sizeof(struct pollfd));
    snd_seq_poll_descriptors(seq_handle, pfd, npfd, POLLIN);

    /* Create ALSA snd_seq_event_t decoder */
    if (snd_midi_event_new(16, &alsa_decoder)) {
        fprintf(stderr, "Error initializing ALSA MIDI decoder!\n");
        exit(1);
    }

    snd_midi_event_reset_decode(alsa_decoder);
    snd_midi_event_no_status(alsa_decoder, 1);

    /* Create interthread ringbuffer */
    jack_ringbuffer = jack_ringbuffer_create(2048);
    if (!jack_ringbuffer) {
        fprintf(stderr, "Failed to create ringbuffer!\n");
        exit(1);
    }
    jack_ringbuffer_reset(jack_ringbuffer);

    /* Create JACK MIDI client */
    jack_client = jack_client_open(client_name, 0, &status);
    if (jack_client == 0) {
        fprintf(stderr, "Failed to connect to JACK server!\n");
        exit(1);
    }

    jack_midi_port = jack_port_register (jack_client, "capture",
                                         JACK_DEFAULT_MIDI_TYPE,
                                         JackPortIsOutput, 0);
    if (!jack_midi_port) {
        fprintf(stderr, "Failed to create JACK MIDI port!\n");
        exit(1);
    }

    jack_set_process_callback(jack_client, jack_callback, 0);

    g_keep_walking = true;

    jack_on_shutdown(jack_client, jack_shutdown, NULL);
    
    signal(SIGINT, &sigint_handler);
    signal(SIGTERM, &sigint_handler);

    if (jack_activate(jack_client)) {
        fprintf(stderr, "Failed to activate JACK client!\n");
        exit(1);
    }

    while (g_keep_walking) {
        if (poll(pfd, npfd, 1000) > 0) {
            midi_action(seq_handle);
        }
        if (jack_write_overflows != old_jack_write_overflows) {
            old_jack_write_overflows = jack_write_overflows;
            fprintf(stderr, "JACK port write overflow count now %d\n", jack_write_overflows);
        }
        // if (events_copied != old_events_copied) {
        //     old_events_copied = events_copied;
        //     fprintf(stderr, "%d events copied\n", events_copied);
        // }
    }

    /* -FIX- provide a way to cleanly exit, and clean up! */
    jack_client_close(jack_client);
    jack_ringbuffer_free(jack_ringbuffer);
    snd_seq_close(seq_handle);

    return 0;
}
