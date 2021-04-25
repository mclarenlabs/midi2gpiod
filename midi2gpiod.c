/*
 * MIDI2GPIOD
 *
 * This program watches for MIDI events from a specified ALSA Sequencer Client
 * and converts Note-On and Note-Off messages to GPIO on/off signals using the
 * libgpiod library.
 *
 * Upon startup, the program attemps to listen for MIDI events from a specified
 * client, which defaults to the portspec `rtpmidi:0`.  To handle cases when
 * the `rtpmidi:0` client is not yet running, `midi2gpiod` continuously watches
 * for new MIDI clients and ports being added to the system, and if the 
 * portspec `rtpmidi:0` is then available, it is attached to.
 *
 * The designated port is configurable on the command line.
 *
 *   $ midi2gpiod -p midikbd:0
 * 
 * McLaren Labs
 * 2021
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <getopt.h>
#include <sys/poll.h>
#include <gpiod.h>
#include <alsa/asoundlib.h>

/*
 * Global Vars for program
 */

int verbose = 0;

/*
 * Configuration for GPIO pins
 */

static char *chipname = "gpiochip0";
static int line1_num = 25;
static int line2_num = 26;
static int line3_num = 27;

struct gpiod_chip *chip;
struct gpiod_line *line1;
struct gpiod_line *line2;
struct gpiod_line *line3;

#ifndef	GPIOD_CONSUMER
#define	GPIOD_CONSUMER	"midi2gpiod"
#endif



/*
 * Configure the MIDI device to listen for.  The portspec defaults to
 * 'rtpmidi:0' - the client and port we attempt to listen to
 */

char *portspec = "rtpmidi:0";


/*
 * When this program starts, it creates an element in the ALSA MIDI system called
 * a `seq` (sequencer).  The `client` is the integer that identifies this `seq`
 * in the system.  A `seq` can have multiple ports.  We create one port, and its
 * index will be zero.
 */

snd_seq_t	*seq;
int		seq_client;
int		seq_port0;

/*
 * These are the names we give our client and port.  These are the names that
 * appear if you run aconnect like this.
 *
 *   $ aconnect -i -o -l
 *
 */

char *sequencer_name =	"midi2gpiod";
char *port_name =	"midi2gpiod";

/*
 * These are the configuration parameters for the type of seq we create.
 */

char *sequencer_type =	"default";
int sequencer_streams = SND_SEQ_OPEN_DUPLEX;
int sequencer_mode =	SND_SEQ_NONBLOCK;

/*
 * These are the capabilities of the port we create.  We want to be able
 * to read and write other seqs.
 */

int  port_caps =  (SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_WRITE |
                   SND_SEQ_PORT_CAP_SUBS_READ | SND_SEQ_PORT_CAP_SUBS_WRITE);
int  port_type = SND_SEQ_PORT_TYPE_MIDI_GENERIC;


void check_snd_err_fatal(const char *msg, int err) {
  if (err < 0) {
    fprintf(stderr, "%s: Alsa error (%s)", msg, snd_strerror(err));
    exit(1);
  }
}

void open_seq(void)
{
  int err;

  err = snd_seq_open(&seq, sequencer_type, sequencer_streams, sequencer_mode);
  check_snd_err_fatal("snd_seq_open", err);

  err = snd_seq_set_client_name(seq, sequencer_name);
  check_snd_err_fatal("snd_seq_set_client_name", err);

  // get the client id for this sequencer
  seq_client = snd_seq_client_id(seq);
}

void create_port(void)
{
  seq_port0 = snd_seq_create_simple_port(seq, port_name, port_caps, port_type);
  check_snd_err_fatal("snd_seq_create_simple_port", seq_port0);
}

/*
 * Attempt to connect from midi port specifed in 'portspec'.
 */

void connect_from_rtpmidi_port(void)
{
  int err;
  snd_seq_addr_t	addr;

  err = snd_seq_parse_address(seq, &addr, portspec);
  if (err < 0) {
    printf("Parsing portspec '%s' failed.  Ignoring.\n", portspec);
    printf("Alsa error (%s)\n", snd_strerror(err));
    return;
  }
    
  err = snd_seq_connect_from(seq, seq_port0, addr.client, addr.port);
  if (err < 0) {
    printf("Connecting from '%s' failed.  Ignoring.\n", portspec);
    printf("Alsa error (%s)\n", snd_strerror(err));
    return;
  }

  printf("Connection from '%s' succeeded\n", portspec);
}

void subscribe_to_system_events(void)
{
  int err;
  err = snd_seq_connect_from(seq, seq_port0, SND_SEQ_CLIENT_SYSTEM, SND_SEQ_PORT_SYSTEM_ANNOUNCE);
  check_snd_err_fatal("snd_seq_connect_from (in subscribe)", err);
}


static void log_event(const snd_seq_event_t *ev)
{
  printf("%3d:%-3d ", ev->source.client, ev->source.port);
  switch (ev->type) {
  case SND_SEQ_EVENT_NOTEON:
    if (ev->data.note.velocity)
      printf("Note on                %2d, note %d, velocity %d\n",
	     ev->data.note.channel, ev->data.note.note, ev->data.note.velocity);
    else
      printf("Note off               %2d, note %d\n",
	     ev->data.note.channel, ev->data.note.note);
    break;
  case SND_SEQ_EVENT_NOTEOFF:
    printf("Note off               %2d, note %d, velocity %d\n",
	   ev->data.note.channel, ev->data.note.note, ev->data.note.velocity);
    break;
  case SND_SEQ_EVENT_CLIENT_START:
    printf("Client start               client %d\n",
	   ev->data.addr.client);
    break;
  case SND_SEQ_EVENT_CLIENT_EXIT:
    printf("Client exit                client %d\n",
	   ev->data.addr.client);
    break;
  case SND_SEQ_EVENT_CLIENT_CHANGE:
    printf("Client changed             client %d\n",
	   ev->data.addr.client);
    break;
  case SND_SEQ_EVENT_PORT_START:
    printf("Port start                 %d:%d\n",
	   ev->data.addr.client, ev->data.addr.port);
    break;
  case SND_SEQ_EVENT_PORT_EXIT:
    printf("Port exit                  %d:%d\n",
	   ev->data.addr.client, ev->data.addr.port);
    break;
  case SND_SEQ_EVENT_PORT_CHANGE:
    printf("Port changed               %d:%d\n",
	   ev->data.addr.client, ev->data.addr.port);
    break;
  case SND_SEQ_EVENT_PORT_SUBSCRIBED:
    printf("Port subscribed            %d:%d -> %d:%d\n",
	   ev->data.connect.sender.client, ev->data.connect.sender.port,
	   ev->data.connect.dest.client, ev->data.connect.dest.port);
    break;
  case SND_SEQ_EVENT_PORT_UNSUBSCRIBED:
    printf("Port unsubscribed          %d:%d -> %d:%d\n",
	   ev->data.connect.sender.client, ev->data.connect.sender.port,
	   ev->data.connect.dest.client, ev->data.connect.dest.port);
    break;
  }
}

void handle_event_note_on(const snd_seq_event_t *ev)
{
  int channel = ev->data.note.channel;
  int note = ev->data.note.note;
  int velocity = ev->data.note.velocity;

  if (verbose)
    printf("Handle note on:%d %d %d\n", channel, note, velocity);

  if (note == 60) {  // middle-C
    gpiod_line_set_value(line1, 1);
  }

  if (note == 62) {
    gpiod_line_set_value(line2, 1);
  }

  if (note == 64) {
    gpiod_line_set_value(line3, 1);
  }
}

void handle_event_note_off(const snd_seq_event_t *ev)
{
  int channel = ev->data.note.channel;
  int note = ev->data.note.note;
  int velocity = ev->data.note.velocity;

  if (verbose)
    printf("Handle note on:%d %d %d\n", channel, note, velocity);

  if (note == 60) {
    gpiod_line_set_value(line1, 0);
  }

  if (note == 62) {
    gpiod_line_set_value(line2, 0);
  }

  if (note == 64) {
    gpiod_line_set_value(line3, 0);
  }
}

void handle_event(const snd_seq_event_t *ev)
{

  switch (ev->type) {

  case SND_SEQ_EVENT_NOTEON:
    handle_event_note_on(ev);
    break;

  case SND_SEQ_EVENT_NOTEOFF:
    handle_event_note_off(ev);
    break;

  case SND_SEQ_EVENT_CLIENT_START:
    connect_from_rtpmidi_port();
    break;

  case SND_SEQ_EVENT_PORT_START:
    connect_from_rtpmidi_port();
    break;
  }
}

void help(const char *pgm)
{
  printf("Usage: %s [-h] [-v] [-p portspec]\n", pgm);
  printf("\n");
  printf("Watch specified MIDI client and translate Note-On/Off to GPIO On/Off\n");
  printf("\n");
  printf("Options:\n");
  printf("  -h, --help\t\tdisplay this message and exit\n");
  printf("  -v, --verbose\t\tlog relevant MIDI messages\n");
  printf("  -p, --portspec=client:port\t\twatch specified MIDI client and port\n");
  return;
}


static int stop = 0;

static void sighandler(int sig)
{
  fprintf(stderr, "SIGHANDLER\n");
  stop = 1;
}


int gpio_setup()
{
  int ret;
  
  chip = gpiod_chip_open_by_name(chipname);
  if (!chip) {
    perror("Open chip failed\n");
    goto end;
  }

  // Configure Line1

  line1 = gpiod_chip_get_line(chip, line1_num);
  if (!line1) {
    perror("Get line1 failed\n");
    goto close_chip;
  }

  ret = gpiod_line_request_output(line1, GPIOD_CONSUMER, 0);
  if (ret < 0) {
    perror("Request line1 as output failed\n");
    goto release_line;
  }

  // Configure Line2

  line2 = gpiod_chip_get_line(chip, line2_num);
  if (!line2) {
    perror("Get line2 failed\n");
    goto close_chip;
  }

  ret = gpiod_line_request_output(line2, GPIOD_CONSUMER, 0);
  if (ret < 0) {
    perror("Request line2 as output failed\n");
    goto release_line;
  }

  // Configure Line3

  line3 = gpiod_chip_get_line(chip, line3_num);
  if (!line3) {
    perror("Get line3 failed\n");
    goto close_chip;
  }

  ret = gpiod_line_request_output(line3, GPIOD_CONSUMER, 0);
  if (ret < 0) {
    perror("Request line3 as output failed\n");
    goto release_line;
  }

  return 1;

 release_line:
  gpiod_line_release(line1);
  gpiod_line_release(line2);
  gpiod_line_release(line3);
 close_chip:
  gpiod_chip_close(chip);
 end:
  return 0;
}
		
int main(int argc, char *argv[])
{

  static const char short_options[] = "hvp:";
  static const struct option long_options[] =
    {
     {"help",	0, NULL, 'h'},
     {"verbose", 0, NULL, 'v'},
     {"port", 1, NULL, 'p'},
     { }
  };

  int c;
  while ((c = getopt_long(argc, argv, short_options,
			  long_options, NULL)) != -1) {
    switch (c) {
    case 'h':
      help(argv[0]);
      return 0;
    case 'v':
      verbose = 1;
      break;
    case 'p':
      portspec = strdup(optarg);
      break;
    default:
      help(argv[0]);
      return 1;
    }
  }

  if (optind < argc) {
    help(argv[0]);
    return 1;
  }

  int err;

  open_seq();
  create_port();
  subscribe_to_system_events();
  connect_from_rtpmidi_port();

  int gpio_ok = gpio_setup();
  if (gpio_ok != 1) {
    fprintf(stderr, "GPIO configuration failed\n");
    exit(1);
  }

  // catch signal to exit when user types ^C
  signal(SIGINT, sighandler);
  signal(SIGTERM, sighandler);

  // file descriptors for alsa seq
  struct pollfd *pfds;
  int npfds;
 
  npfds = snd_seq_poll_descriptors_count(seq, POLLIN);
  pfds = alloca(sizeof(*pfds) * npfds);

  for (;;) {

    snd_seq_poll_descriptors(seq, pfds, npfds, POLLIN);
    if (poll(pfds, npfds, -1) < 0)
      break;

    do {
      snd_seq_event_t *event;
      err = snd_seq_event_input(seq, &event);

      if (err < 0)
	break;

      if (event) {
	if (verbose)
	  log_event(event);

	handle_event(event);
      }
      
    } while (err > 0);

    if (stop)
      break;
    
  }

 release_line:
  gpiod_line_release(line1);
  gpiod_line_release(line2);
  gpiod_line_release(line3);
 close_chip:
  gpiod_chip_close(chip);
}
