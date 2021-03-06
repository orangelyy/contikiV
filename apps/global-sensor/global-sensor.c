// Sets up the global package for 'Active Sensor'

#include "global-sensor.h"
#include "contiki.h"
#include "shell.h"

#include "dev/leds.h"
#include "dev/light-sensor.h"
#include "dev/sht11-sensor.h"
#include "dev/battery-sensor.h"
#include "dev/serial-line.h"
#include "net/rime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>


#define SENSOR_CHANNEL 133
#define SENSOR_INIT_DIV 10

#define DEBUG

/*
 * Global sensor_sel definition - default to temperature
 * This holds the current active sensor
 */
unsigned char sensor_sel = 't';

/*---------------------------------------------------------------------------*/
PROCESS( shell_sensor_select_process, "sensor-sel");
PROCESS( shell_sensor_read_process, "sensor-read");
PROCESS( broadcast_sensor_set_process, "sensor-setall");
PROCESS( broadcast_sensor_read_proc, "sensor-readall");

PROCESS( sensor_set_dispatcher, "dispatcher" );
PROCESS( sensor_set, "sensor-set" );
PROCESS( sensor_unset, "sensor-unset" );
PROCESS( sensor_read_proc, "sensor-read");
PROCESS( sensor_print_data, "sensor-print");

//PROCESS( global_sensor_init, "gs-init");

//AUTOSTART_PROCESSES( &global_sensor_init );

SHELL_COMMAND(sensor_select_command,
              "sensor-sel",
              "sensor-sel [char]: set active sensor to [char]",
              &shell_sensor_select_process);
SHELL_COMMAND(sensor_read_command,
              "sensor-read",
              "sensor-read : read current active sensor",
              &shell_sensor_read_process);
SHELL_COMMAND(sensor_set_all_command,
              "sensor-setall",
              "sensor-setall [char]: Broadcast message to set all sensors to [char].",
              &broadcast_sensor_set_process);
SHELL_COMMAND(sensor_readall_command,
              "sensor-readall",
              "sensor-readall : read current active sensor of all nodes",
              &broadcast_sensor_read_proc);

/*---------------------------------------------------------------------------*/
static void
broadcast_recv(struct broadcast_conn *c, const rimeaddr_t *from)
{
  /*
   * This function is run when a broadcast message is received.  This will
   * start a process to handle the commands received.
   */
  process_start( &sensor_set_dispatcher, (char *)packetbuf_dataptr() );
}

static const struct broadcast_callbacks broadcast_call = {broadcast_recv};
static struct broadcast_conn broadcast;
/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
PROCESS_THREAD( sensor_set_dispatcher, ev, data )
{
  PROCESS_BEGIN();

  /*
   * Start a process to handle the incoming broadcast command
   */
  int i;
  char cmd[7];

  if( data != NULL )
  {

#ifdef DEBUG
    printf("broadcast message received: %c %c %d.%d\n",
           ((char*)data)[0], ((char*)data)[1],
           ((char*)data)[2], ((char*)data)[3]);
#endif /* DEBUG */

    // Copy command
    for( i=0; i<5; i++ )
      cmd[i] = ((char*)data)[i];


    switch( cmd[0] )
    {
      case 's':
        // Set sensor
        process_start( &sensor_set, cmd );
        break;
      case 'u':
        // Unset sensor
        process_start( &sensor_unset, cmd );
        break;
      case 'r':
        // Read sensor
        process_start( &sensor_read_proc, cmd );
        break;
      case 'd':
        // Copy extra data and display received data
        cmd[5] = ((char*)data)[5];
        cmd[6] = ((char*)data)[6];
        process_start( &sensor_print_data, cmd );
        break;
    }

  }
  else
  {
#ifdef DEBUG
    printf("No data received!\n");
#endif
  }
  PROCESS_END();
}

PROCESS_THREAD( sensor_set, ev, data )
{
  PROCESS_BEGIN();

  unsigned char cmd[7];
  unsigned char a0, a1;

  a0 = (unsigned char)(rimeaddr_node_addr.u8[0]);
  a1 = (unsigned char)(rimeaddr_node_addr.u8[1]);

  parse_shell_command( (unsigned char*)data, cmd );

  #ifdef DEBUG
  printf("Addr: %d.%d\n", cmd[2], cmd[3]);
  #endif


  if( (cmd[2] == a0 && cmd[3] == a1) ||
    (cmd[2] == 0  || cmd[3] == 0 ) )
    sensor_init( ((char*)data)[1] );

  PROCESS_END();
}

PROCESS_THREAD( sensor_unset, ev, data )
{
  PROCESS_BEGIN();

  unsigned char cmd[7];
  unsigned char a0, a1;

  a0 = (unsigned char)(rimeaddr_node_addr.u8[0]);
  a1 = (unsigned char)(rimeaddr_node_addr.u8[1]);

  parse_shell_command( (unsigned char*)data, cmd );

  if( (cmd[2] == a0 && cmd[3] == a1) ||
    (cmd[2] == 0  || cmd[3] == 0 ) )
    sensor_uinit( ((char*)data)[1] );

  PROCESS_END();
}

PROCESS_THREAD( sensor_read_proc, ev, data )
{
  PROCESS_BEGIN();
  unsigned char cmd[7];
  char my_data[7];
  uint16_t reading = sensor_read();
  unsigned char a0, a1;

  a0 = (unsigned char)(rimeaddr_node_addr.u8[0]);
  a1 = (unsigned char)(rimeaddr_node_addr.u8[1]);

  parse_shell_command( (unsigned char*)data, cmd );

  if( (cmd[2] == a0 && cmd[3] == a1) ||
      (cmd[2] == 0  || cmd[3] == 0 ) )
  {
    my_data[0] = 'd';
    my_data[1] = 'x';
    my_data[2] = a0;
    my_data[3] = a1;

    // data[4] will store upper byte, data[5] will store lower bye
    my_data[4] = (reading >> 8)&0x00FF;
    my_data[5] = reading&0x00FF;

    my_data[6] = '\0';

#ifdef DEBUG
    printf("Reading %d.%d:%i\n", cmd[2], cmd[3], reading);
#endif

    // Broadcast data
    packetbuf_copyfrom(my_data, 7);
    broadcast_send(&broadcast);
  }

  PROCESS_END();
}

PROCESS_THREAD( sensor_print_data, ev, data )
{
  PROCESS_BEGIN();
  uint16_t reading = 0;
  char* d = (char*)data;

  reading = d[4];
  reading = reading << 8;
  reading |= d[5];

  printf("%d.%d:%i\n", d[2], d[3], reading);

  PROCESS_END();
}

PROCESS_THREAD( broadcast_sensor_read_proc, ev, data )
{
  PROCESS_BEGIN();

  /*
   * Broadcasts a message to all/given sensors telling them to
   * transmit a reading.
   */

  unsigned char cmd[6];
  int retval;

  retval = parse_shell_command( data, cmd );

  if( retval == -1 )
  {
    printf("Could not parse args:\n%s\nReturn value: %i\n", (char*)data, retval);
  }
  else
  {
    cmd[0] = 'r';
#ifdef DEBUG
    printf( "Sending %c %c %d.%d\n", cmd[0], cmd[1], cmd[2], cmd[3]);
#endif

    packetbuf_copyfrom(cmd, 6);
    broadcast_send(&broadcast);
  }

  PROCESS_END();
}


PROCESS_THREAD( broadcast_sensor_set_process, ev, data )
{
  PROCESS_BEGIN();
  /*
   * This process sends a broadcast message on the #define'd sensor channel.
   * The actual sensor switching is done in the broadcast callback.
   * The command string sent has the following structure:
   *    cmd[0]: Command char - s (set), r (read), u (unset)
   *    cmd[1]: Sensor char - v,i,l,s,t,h,x
   *    cmd[2]: Null terminator.
   */
  unsigned char cmd[6];
  int retval;

  retval = parse_shell_command( data, cmd );
  if( retval < 0 )
  {
    printf("Could not parse args:\n%s\nReturn value: %i\n", (char*)data, retval);
  }
  else if( (cmd[1] = valid_sensor(cmd[1]) ) == 0 )
  {
    printf("Must choose one of {v,i,l,s,t,h,x}\nCurrently = %c\n", sensor_sel);
  }
  else
  {
    if( cmd[1] == 'x' )
    {
      cmd[0] = 'u';
    }
    else
    {
      cmd[0] = 's';
    }
    packetbuf_copyfrom(&cmd, 6);
    broadcast_send(&broadcast);
  }

  PROCESS_END();
}

PROCESS_THREAD(shell_sensor_read_process, ev, data)
{
  PROCESS_BEGIN();
  printf( "%d.%d:%i\n", (int)(rimeaddr_node_addr.u8[0]),
                        (int)(rimeaddr_node_addr.u8[1]),
                        sensor_read() );
  PROCESS_END();
}

// PROCESS_THREAD( global_sensor_init, ev, data )
// {
//   PROCESS_BEGIN();
//   /*
//    * This is the initialization process.  It should only run once when the system
//    * boots.  Right now, it opens the sensor channel and defines the callbacks.
//    */
//
//   /* Opens a broacast channel - MUST BE CLOSED LATER! */
//   broadcast_open(&broadcast, SENSOR_CHANNEL, &broadcast_call);
//
//
//   PROCESS_END();
// }

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(shell_sensor_select_process, ev, data) {

//	static char temp_var = 'x';
  static struct etimer et;

	PROCESS_BEGIN();

  /*
   * Initialize sensor
   * First, convert the data into a char we can use.
   * Then, check if it is valid.  valid_sensor will return 0 if invalid.
   * Finally, initialize sensor.  If invalid, sensor_init won't do anything.
   */
  unsigned char s;

  if( (s = valid_sensor( shell_datatochar(data) )) == 0 )
    printf("Must choose one of {v,i,l,s,t,h,x}\nCurrently = %c\n", sensor_sel);
  else if( s != sensor_sel )
  {
    /* If s is current sensor, nothing to do */
    sensor_init( s );

    /* Wait for sensor to initialize */
    etimer_set(&et, CLOCK_SECOND / SENSOR_INIT_DIV);
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
  }

    // Old Code
    {
      //   sensor_init
      //   (
      //     valid_sensor
      //     (
      //       shell_datatochar(data)
      //     )
      //   );


      // 	if(temp_var == 'v') {
      // 		sensor_sel = 'v';
      // 	} else if(temp_var == 'i') {
      // 		sensor_sel = 'i';
      // 	} else if(temp_var == 'l') {
      // 		sensor_sel = 'l';
      // 	} else if(temp_var == 's') {
      // 		sensor_sel = 's';
      // 	} else if(temp_var == 't') {
      // 		sensor_sel = 't';
      // 	} else if(temp_var == 'h') {
      // 		sensor_sel = 'h';
      // 	} else {
      // 		printf("Must choose one of {v,i,l,s,t,h}\n");
      // 		printf("Currently = %c\n", sensor_sel);
      // 	}
    }

	PROCESS_END();
}
/*---------------------------------------------------------------------------*/
void shell_global_sensor_init(void)
{
	shell_register_command(&sensor_select_command);
  shell_register_command(&sensor_set_all_command);
  shell_register_command(&sensor_read_command);
  shell_register_command(&sensor_readall_command);

  broadcast_open(&broadcast, SENSOR_CHANNEL, &broadcast_call);
}

unsigned char shell_datatochar(const unsigned char *str)
{
  const unsigned char *strptr = str;

	if(str == NULL) {
		return 0;
	}

	while( isspace(*strptr) )
  {
		++strptr;
	}

	return *strptr;
}

int parse_shell_command( unsigned char* cmd, unsigned char* ret )
{
  /*
   * Parses cmd and places the parsed message in ret at
   * ret[1] -- ret[5].
   * ret must be a 6+ byte char array!
   * Returns 0 on success, -1 on NULL argument, -2 on empty
   * command.
   *
   * cmd is of the form: <sensor> <addr0.addr1>
   * 'sensor' is a single char, 'addr0' and 'addr1' 0-255
   */

//  int i, num=0;
  unsigned char* cmdarr[3];


  if( NULL == cmd || NULL == ret )
    return -1;

  // Get tokens
  cmdarr[0] = (unsigned char*)strtok( (char*)cmd, " " );
  cmdarr[1] = (unsigned char*)strtok(NULL, " ");
  cmdarr[2] = (unsigned char*)strtok(NULL, " ");

  // cmdarr[1] can be null if only one argument
  if( cmdarr[0] == NULL ) // || cmdarr[1] == NULL
  {
    ret[1] = 'x';
    ret[2] = 0;
    ret[3] = 0;
    ret[4] = 0;
    ret[5] = '\0';
    return -2;
  }

  // Get rest of tokens, up to fifth
  //for( i=1; i<2 && (cmdarr[i-1] != NULL); i++ )
  //  cmdarr[i] = strtok(NULL, " ");

  ret[1] = *(cmdarr[0]);

  // Now parse second token to get addr
  if( cmdarr[1] != NULL )
  {
    ret[2] = (unsigned char)atoi( strtok( (char*)(cmdarr[1]), "." ) );
    ret[3] = (unsigned char)atoi( strtok( NULL, "." ) );
#ifdef DEBUG
    printf("Parsed %d.%d\n", ret[2], ret[3]);
#endif

    if( cmdarr[2] != NULL )
      ret[4] = (unsigned char)atoi( (char*)(cmdarr[2]) );
    else
      ret[4] = '\0';
  }
  else
  {
    ret[2] = 0;
    ret[3] = 0;
    ret[4] = 0;
  }
  ret[5] = '\0';
  return 0;
}

/*
 * Initializes a sensor.  Does not wait for it to initialize
 */
unsigned char sensor_init(unsigned char s) {
  /* Keep a static version so s doesn't get clobbered by PROCESS_WAIT_EVENT_UNTIL */
  static unsigned char ss;
  ss = s;
  //static struct etimer et;

   /* Do some sanity checks. */
  if( s == sensor_sel || 0 == valid_sensor(s) )
    return sensor_sel;

  /* Deactivate current sensor */
  sensor_uinit( s );

  /* Activate new sensor */
  switch( s ) {
		case 'v': 
			SENSORS_ACTIVATE(battery_sensor);
			break;
		case 'i':
			SENSORS_ACTIVATE(sht11_sensor);
			break;
		case 't':
			SENSORS_ACTIVATE(sht11_sensor);
			break;
		case 'h':
			SENSORS_ACTIVATE(sht11_sensor);
			break;
		case 'l':
			SENSORS_ACTIVATE(light_sensor);
			break;
		case 's':
			SENSORS_ACTIVATE(light_sensor);
			break;
    case 'x':
      /* Special case - deactivate current sensor */
      return sensor_uinit( s );
	}

// 	/* Wait for sensor to initialize */
//   etimer_set(&et, CLOCK_SECOND / SENSOR_INIT_DIV);
//   PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));

	/* Update sensor_sel and return */
	return sensor_sel = ss;
}

// Switches off the currently selected sensor.
unsigned char sensor_uinit(unsigned char s) {
  switch( valid_sensor(s) ) {
		case 'v': 
			SENSORS_DEACTIVATE(battery_sensor);
			break;
		case 'i':
			SENSORS_DEACTIVATE(sht11_sensor);
			break;
		case 't':
			SENSORS_DEACTIVATE(sht11_sensor);
			break;
		case 'h':
			SENSORS_DEACTIVATE(sht11_sensor);
			break;
		case 'l':
			SENSORS_DEACTIVATE(light_sensor);
			break;
		case 's':
			SENSORS_DEACTIVATE(light_sensor);
			break;
    case 0:
      break;
	}

	/* Set sensor_sel to 0 to indicate no active sensors */
  sensor_sel = 0;
	return s;
}

// Reads the currently selected sensor.
uint16_t sensor_read(void) {
	switch(sensor_sel) {
		case 'v': 
			return battery_sensor.value(0);
		case 'i':
			return sht11_sensor.value(SHT11_SENSOR_BATTERY_INDICATOR);
		case 't':
			return sht11_sensor.value(SHT11_SENSOR_TEMP);
		case 'h':
			return sht11_sensor.value(SHT11_SENSOR_HUMIDITY);
		case 'l':
			return light_sensor.value(LIGHT_SENSOR_PHOTOSYNTHETIC);
		case 's':
			return light_sensor.value(LIGHT_SENSOR_TOTAL_SOLAR);
	}

	// Else error, return 0
	return 0;
}

unsigned char valid_sensor( unsigned char s )
{
  /*
   * Returns s if valid sensor, otherwise 0
   */
  s = tolower( s );

  switch (s)
  {
    case 'v':
    case 'i':
    case 't':
    case 'h':
    case 'l':
    case 's':
      return s;
    case 'x':
      return sensor_sel;
    default:
      return 0;
  }
}
