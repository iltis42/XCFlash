#include "Flarm.h"
#include "logdef.h"
#include "Colors.h"
#include "math.h"
#include "pflaa2.h"
#include <iostream>
#include <sstream>

#define TASK_PERIOD 250  // ms

#define FLARM_TIMEOUT (10* (1000/TASK_PERIOD))
#define PFLAU_TIMEOUT (10* (1000/TASK_PERIOD))

#define CENTERX 120
#define CENTERY 120
#define DISTANCE_FAR 1000.0

#define RTD(x) (x*RAD_TO_DEG)
#define DTR(x) (x*DEG_TO_RAD)

int Flarm::RX = 0;
int Flarm::TX = 1;
int Flarm::GPS = 1;
int Flarm::Power = 0;
int Flarm::AlarmLevel = 0;
int Flarm::RelativeBearing = 0;
int Flarm::AlarmType = 0;
int Flarm::RelativeVertical = 0;
int Flarm::RelativeDistance = 0;
float Flarm::gndSpeedKnots = 0;
float Flarm::gndCourse = 0;
bool Flarm::myGPS_OK = false;
char Flarm::ID[20] = "";
int Flarm::bincom = 0;
int Flarm::pflau_timeout = PFLAU_TIMEOUT;
int Flarm::pflaa_timeout = 0;
TaskHandle_t Flarm::pid = 0;
AdaptUGC* Flarm::ucg;
e_audio_alarm_type_t Flarm::alarm = AUDIO_ALARM_OFF;

int Flarm::oldDist = 0;
int Flarm::oldVertical = 0;
int Flarm::oldBear = 0;
int Flarm::alarmOld=0;
int Flarm::_tick=0;
int Flarm::timeout=0;
int Flarm::ext_alt_timer=0;
int Flarm::_numSat=0;
int Flarm::bincom_port=0;
float Flarm::closest_object=DISTANCE_FAR;
bool Flarm::flarm_sim = false;

extern xSemaphoreHandle spiMutex;

#define END_SIM NUM_PFLAA2_SIM
// #define END_SIM 200

int Flarm::sim_tick=0;

void Flarm::begin(){
	xTaskCreatePinnedToCore(&taskFlarm, "taskFlarm", 4096, NULL, 14, &pid, 0);
}

void Flarm::taskFlarm(void *pvParameters)
{
	while(1){
		progress();
		delay(TASK_PERIOD);
		_tick++;
	}
}


/*
PFLAA,<AlarmLevel>,<RelativeNorth>,<RelativeEast>,<RelativeVertical>,<IDType>,<ID>,<Track>,<TurnRate>,<GroundSpeed>,<ClimbRate>,<AcftType>
e.g.
$PFLAA,0,-1234,1234,220,2,DD8F12,180,,30,-1.4,1*

 */


void Flarm::parsePFLAA( const char *pflaa ){
	/*
	http://delta-omega.com/download/EDIA/FLARM_DataportManual_v3.02E.pdf

		Periodicity:
				sent when available and port Baud rate is sufficient, can be sent several times per second with
				information on several (but maybe not all) targets around.

		PFLAA,<AlarmLevel>,<RelativeNorth>,<RelativeEast>,<RelativeVertical>,<IDType>,<ID>,<Track>,<TurnRate>,<GroundSpeed>,<ClimbRate>,<Type>

		<AlarmLevel>
					Alarm level as assessed by FLARM
					0 = no alarm (pure traffic, limited to 2km range and 500m altitude difference)
					1 = low-level alarm
					2 = important alarm
					3 = urgent alarm
		<RelativeNorth>
					Relative position in Meter true north from own position, signed integer
		<RelativeEast>
					Relative position in Meter true east from own position, signed integer
		<RelativeVertical>
					Relative vertical separation in Meter above own position, negative values indicate
					the other aircraft is lower, signed integer. Some distance-dependent random noise
					is applied to altitude data if the privacy for the target is active.
		<ID-Type>
					Defines the interpretation of the following field <ID>
					0 = stateless random-hopping pseudo-ID (chosen by FLARM)
					1 = official ICAO aircraft address
					2 = stable FLARM pseudo-ID (chosen by FLARM)
		<ID>
					6-digit hex value (e.g. "5A77B1") as configured in the target's PFLAC,ID sentence.
					The interpretation is delivered in <ID-Type>
		<Track>
					The target's true ground track in degrees. Integer between 0 and 359. The value 0
					indicates a true north track. This field is empty if the privacy for the target is active.
		<TurnRate>
					The target's turn rate. Positive values indicate a clockwise turn. Signed decimal
					value in °/s. Currently omitted. Field is empty if the privacy for the target is active.
		<GroundSpeed>
					The target's ground speed. Decimal value in m/s. The field is set to 0 to indicate
					the aircraft is not moving, i.e. on ground. This field is empty if the privacy for the
					target is active while the target is airborne.
		<ClimbRate>
					The target's climb rate. Positive values indicate a climbing aircraft. Signed decimal
					value in m/s. This field is empty if the privacy for the target is active.
		<Type>
					Up to two hex characters showing the object type
					0 = unknown
					1 = glider
					2 = tow plane
					3 = helicopter
					4 = parachute
					5 = drop plane
					6 = fixed hang-glider
					7 = soft para-glider
					8 = powered aircraft
					9 = jet aircraft
					A = UFO
					B = balloon
					C = blimp, zeppelin
					D = UAV
					F = static
	 */
	int cs;
	nmea_pflaa_s PFLAA;
	memset( &PFLAA, 0, sizeof(PFLAA) );
	int calc_cs=calcNMEACheckSum( pflaa );
	cs = getNMEACheckSum( pflaa );
	if( cs != calc_cs ){
		ESP_LOGW(FNAME,"PFLAA CHECKSUM ERROR: %s; calculcated CS: %d != delivered CS %d", pflaa, calc_cs, cs );
		return;
	}
	// PFLAA,<AlarmLevel>,<RelativeNorth>,<RelativeEast>,<RelativeVertical>,<IDType>,<ID>,<Track>,<TurnRate>,<GroundSpeed>,<ClimbRate>,<Type>

	std::istringstream ss(pflaa);
	std::string token;
	std::getline(ss, token, ',');
	std::getline(ss, token, ',');
	if( !token.empty() )
		PFLAA.alarmLevel = std::stoi(token);
	std::getline(ss, token, ',');
	if( !token.empty() )
		PFLAA.relNorth = std::stoi(token);
	std::getline(ss, token, ',');
	if( !token.empty() )
		PFLAA.relEast = std::stoi(token);
	std::getline(ss, token, ',');
	if( !token.empty() )
		PFLAA.relVertical = std::stoi(token);
	std::getline(ss, token, ',');
	if( !token.empty() )
		PFLAA.idType = std::stoi(token);
	std::getline(ss, token, ',');
	if( !token.empty() )
		sscanf(token.c_str(),"%06X", &PFLAA.ID );
	std::getline(ss, token, ',');
	if( !token.empty() )
		PFLAA.track = std::stoi(token);
	std::getline(ss, token, ',');
	if( !token.empty() )
		PFLAA.turnRate = std::stoi(token);
	std::getline(ss, token, ',');
		if( !token.empty() )
	PFLAA.groundSpeed = std::stoi(token);
	std::getline(ss, token, ',');
	if( !token.empty() )
		PFLAA.climbRate = std::stof(token);
	std::getline(ss, token, ',');
	if( !token.empty() )
		sscanf(token.c_str(), "%s", PFLAA.acftType);

	_tick=0;
	timeout = FLARM_TIMEOUT;
	if( PFLAA.groundSpeed > 3.6 ){  // only regard moving objects
		float distance = sqrt( PFLAA.relNorth*PFLAA.relNorth + PFLAA.relEast*PFLAA.relEast )/1000.0;
		if( closest_object > distance ){
			closest_object = distance;
		}
	}
}




// Calculate the checksum and output it as an int
// is required as HEX in the NMEA data set
// between $ or! and * character
int Flarm::calcNMEACheckSum(const char *nmea) {
	int i, XOR, c;
	for (XOR = 0, i = 0; i < strlen(nmea); i++) {
		c = (unsigned char)nmea[i];
		if (c == '*') break;
		if ((c != '$') && (c != '!')) XOR ^= c;
	}
	return XOR;
}

int Flarm::getNMEACheckSum(const char *nmea) {
	int i, cs, c;
	for (i = 0; i < strlen(nmea); i++) {
		c = (unsigned char)nmea[i];
		if (c == '*') break;
	}
	sscanf( &nmea[i],"*%02x", &cs );
	return cs;
}

void Flarm::flarmSim(){
	// ESP_LOGI(FNAME,"flarmSim sim-tick: %d", sim_tick);
	if( sim_tick < END_SIM  ){
		if( sim_tick >= 0 ){
			// int cs = calcNMEACheckSum( (char *)pflaa2[sim_tick] );
			char str[80];
			sprintf( str, "%s", pflaa2[sim_tick] );
			parseNMEA( str, strlen(str) );
			// ESP_LOGI(FNAME,"Serial FLARM SIM: %s",  str );
		}
		if( !(_tick%2) )
			sim_tick++;
	}else{
		flarm_sim=false; // end sim mode
	}
}

void Flarm::progress(){  //  per second
	if( timeout ){
		timeout--;
	}
	if( timeout == 0 ){
		myGPS_OK = false;
		closest_object = DISTANCE_FAR;
	}
	// ESP_LOGI(FNAME,"progress, timeout=%d", timeout );
	if( flarm_sim ){
		flarmSim();
		flarmSim();
	}else{  // no PFLAU in flarm Simulation
		if( pflau_timeout ){
			pflau_timeout--;
			if( pflau_timeout == 0 ){
				TX = 0;
				GPS = 0;
			}
		}
	}
}

void Flarm::parseNMEA( const char *str, int len ){
	// ESP_LOGI(FNAME,"parseNMEA: %s, len: %d", str,  strlen(str) );

	if( !strncmp( str+1, "PFLAE,", 5 )) {  // On Task declaration or re-connect
		parsePFLAE( str );
	}
	else if( !strncmp( str+1, "PFLAU,", 5 )) {
		parsePFLAU( str );
	}
	else if( !strncmp( str+1, "PFLAA,", 5 )) {
		parsePFLAA( str );
	}
	else if( !strncmp( str+3, "RMC,", 3 ) ) {
		parseGPRMC( str );
	}
	else if( !strncmp( str+3, "GGA,", 3 )) {
		parseGPGGA( str );
	}
	else if( !strncmp( str+3, "RMZ,", 3 )) {
		parsePGRMZ( str );
	}
}


bool Flarm::connected(){
	// ESP_LOGI(FNAME,"timeout=%d", timeout );
	if( timeout > 0 )
		return true;
	else
		return false;
};

/*
eg1. $GPRMC,081836,A,3751.65,S,14507.36,E,000.0,360.0,130998,011.3,E*62
eg2. $GPRMC,225446,A,4916.45,N,12311.12,W,000.5,054.7,191194,020.3,E*68
     $GPRMC,201914.00,A,4857.58740,N,00856.94735,E,0.172,122.95,310321,,,A*6D

           225446.00    Time of fix 22:54:46 UTC
           A            Navigation receiver warning A = OK, V = warning
           4916.45,N    Latitude 49 deg. 16.45 min North
           12311.12,W   Longitude 123 deg. 11.12 min West
           000.5        Speed over ground, Knots
           054.7        Course Made Good, True
           191194       Date of fix  19 November 1994
           020.3,E      Magnetic variation 20.3 deg East
 *68          mandatory checksum


 */
void Flarm::parseGPRMC( const char *gprmc ) {
	char warn;
	int cs;
	int calc_cs=calcNMEACheckSum( gprmc );
	cs = getNMEACheckSum( gprmc );
	if( cs != calc_cs ){
		ESP_LOGW(FNAME,"CHECKSUM ERROR: %s; calculcated CS: %d != delivered CS %d", gprmc, calc_cs, cs );
		return;
	}
	sscanf( gprmc+3, "RMC,%*f,%c,%*f,%*c,%*f,%*c,%f,%f,%*d,%*f,%*c*%*02x", &warn, &gndSpeedKnots, &gndCourse);

	//ESP_LOGI(FNAME,"GPRMC myGPS_OK %d warn %c", myGPS_OK, warn );
	if( warn == 'A' ) {
		if( myGPS_OK == false ){
			myGPS_OK = true;
			ESP_LOGI(FNAME,"GPRMC, GPS status changed to good, rmc:%s gps:%d", gprmc, myGPS_OK );
		}
		// ESP_LOGI(FNAME,"Track: %3.2f, GPRMC: %s", gndCourse, gprmc );
	}
	else{
		if( myGPS_OK == true  ){
			myGPS_OK = false;
			ESP_LOGI(FNAME,"GPRMC, GPS status changed to bad, rmc:%s gps:%d", gprmc, myGPS_OK );
		}
	}
	timeout = FLARM_TIMEOUT;
	// ESP_LOGI(FNAME,"parseGPRMC() GPS: %d, Speed: %3.1f knots, Track: %3.1f° ", myGPS_OK, gndSpeedKnots, gndCourse );
}

/*
  GPGGA

hhmmss.ss = UTC of position
llll.ll = latitude of position
a = N or S
yyyyy.yy = Longitude of position
a = E or W
x = GPS Quality indicator (0=no fix, 1=GPS fix, 2=Dif. GPS fix)
xx = number of satellites in use
x.x = horizontal dilution of precision
x.x = Antenna altitude above mean-sea-level
M = units of antenna altitude, meters
x.x = Geoidal separation
M = units of geoidal separation, meters
x.x = Age of Differential GPS data (seconds)
xxxx = Differential reference station ID

eg. $GPGGA,hhmmss.ss,llll.ll,a,yyyyy.yy,a,x,xx,x.x,x.x,M,x.x,M,x.x,xxxx*hh
    $GPGGA,121318.00,4857.58750,N,00856.95715,E,1,05,3.87,247.7,M,48.0,M,,*52
 */


void Flarm::parseGPGGA( const char *gpgga ) {
	// ESP_LOGI(FNAME,"parseGPGGA");
	int numSat;
	int cs;
	// ESP_LOGI(FNAME,"parseG*GGA: %s", gpgga );
	int calc_cs=calcNMEACheckSum( gpgga );
	cs = getNMEACheckSum( gpgga );
	if( cs != calc_cs ){
		ESP_LOGW(FNAME,"CHECKSUM ERROR: %s; calculcated CS: %d != delivered CS %d", gpgga, calc_cs, cs );
		return;
	}
	int ret=sscanf( gpgga+3, "GGA,%*f,%*f,%*c,%*f,%*c,%*d,%d,%*f,%*f,M,%*f,M,%*f,%*d*%*02x", &numSat);
	// ESP_LOGI(FNAME,"parseG*GGA: %s numSat=%d ssf_ret=%d", gpgga, numSat, ret );
	if( ret >=1 ){
		if( numSat != _numSat ){
			_numSat = numSat;
		}
		timeout = FLARM_TIMEOUT;
	}
}

// parsePFLAE $PFLAE,A,0,0*33


void Flarm::parsePFLAE( const char *pflae ) {
	ESP_LOGI(FNAME,"parsePFLAE %s", pflae );
	int cs;
	int calc_cs=calcNMEACheckSum( pflae );
	cs = getNMEACheckSum( pflae );
	if( cs != calc_cs ){
		ESP_LOGW(FNAME,"CHECKSUM ERROR: %s; calculcated CS: %d != delivered CS %d", pflae, calc_cs, cs );
		return;
	}
	timeout = FLARM_TIMEOUT;
	const char* pf = "$PFLAE,A,0,0";
	const unsigned short len = strlen(pf);
	if( !strncmp( pflae, pf, len )  ){
		ESP_LOGI(FNAME,"got PFLAE");
	}
}


/* PFLAU,<RX>,<TX>,<GPS>,<Power>,<AlarmLevel>,<RelativeBearing>,<AlarmType>,<RelativeVertical>,<RelativeDistance>,<ID>
		$PFLAU,3,1,2,1,2,-30,2,-32,755*FLARM is working properly and currently receives 3 other aircraft.
		The most dangerous of these aircraft is at 11 o’clock, position 32m below and 755m away. It is a level 2 alarm


<TX>
Decimal integer value. Range: from 0 to 1.
Transmission status: 1 for OK and 0 for no transmission

<GPS>
Decimal integer value. Range: from 0 to 2.
GPS status:
0 = no GPS reception
1 = 3d-fix on ground, i.e. not airborne
2 = 3d-fix when airborne
If <GPS> goes to 0, FLARM will not work. Nevertheless,
wait for some seconds to issue any warnings

<AcftType>
0 = unknown
1 = glider / motor glider
2 = tow / tug plane
3 = helicopter / rotorcraft
4 = skydiver
5 = drop plane for skydivers
6 = hang glider (hard)
7 = paraglider (soft)
8 = aircraft with reciprocating engine(s)
9 = aircraft with jet/turboprop engine(s)
A = unknown
B = balloon
C = airship
D = unmanned aerial vehicle (UAV)
E = unknown
F = static object
 */

void Flarm::parsePFLAU( const char *pflau, bool sim_data ) {
	// ESP_LOGI(FNAME,"parsePFLAU");
	int cs;
	int id;
	int calc_cs=calcNMEACheckSum( pflau );
	cs = getNMEACheckSum( pflau );
	if( cs != calc_cs ){
		ESP_LOGW(FNAME,"CHECKSUM ERROR: %s; calculcated CS: %d != delivered CS %d", pflau, calc_cs, cs );
		return;
	}
	sscanf( pflau, "$PFLAU,%d,%d,%d,%d,%d,%d,%d,%d,%d,%x*%02x",&RX,&TX,&GPS,&Power,&AlarmLevel,&RelativeBearing,&AlarmType,&RelativeVertical,&RelativeDistance,&id,&cs);
	// ESP_LOGI(FNAME,"parsePFLAU() RB: %d ALT:%d  DIST %d",RelativeBearing,RelativeVertical, RelativeDistance );
	sprintf( ID,"%06x", id );
	_tick=0;
	timeout = FLARM_TIMEOUT;
	pflau_timeout = PFLAU_TIMEOUT;
}

void Flarm::parsePFLAX( const char *msg, int port ) {
	// ESP_LOGI(FNAME,"parsePFLAX");
	// ESP_LOG_BUFFER_HEXDUMP(FNAME, msg.c_str(), msg.length(), ESP_LOG_INFO);
	int start=0;
	/* Solved now by DataLink frame recognition
	if( !strncmp( msg, "\n", 1 )  ){  // catch case when serial.cpp does not correctly dissect at '\n', needs further evaluation, maybe multiple '\n' sent ?
		start=1;
	}
	 */
	// Note, if the Flarm switch to binary mode was accepted, Flarm will answer
	// with $PFLAX,A*2E. In error case you will get as answer $PFLAX,A,<error>*
	// and the Flarm stays in text mode.
	const char* pflax = "$PFLAX,A*2E";
	const unsigned short lenPflax = strlen(pflax);

	if( strlen(msg + start) >= lenPflax && !strncmp(  msg + start, pflax, lenPflax )  ){
		bincom_port = port;
		int old = bincom;
		bincom = 5;
		ESP_LOGI(FNAME,"bincom: %d --> %d", old, bincom  );
		timeout = FLARM_TIMEOUT;
	}
}

void Flarm::drawDownloadInfo() {
	// ESP_LOGI(FNAME,"---> Flarm::drawDownloadInfo is called"  );
	xSemaphoreTake(spiMutex, portMAX_DELAY );
	ucg->setColor( COLOR_WHITE );
	ucg->setFont(ucg_font_fub20_hr);
	ucg->setPrintPos(60, 140);
	ucg->printf(PROGMEM"Flarm IGC");
	ucg->setPrintPos(60, 170);
	ucg->printf(PROGMEM"download");
	ucg->setPrintPos(60, 200);
	ucg->printf(PROGMEM"is running");
	ucg->setFont(ucg_font_fub11_hr);
	ucg->setPrintPos(20, 280);
	ucg->printf(PROGMEM"(restarts on end download)");
	xSemaphoreGive(spiMutex);
}



// $PGRMZ,880,F,2*3A  $PGRMZ,864,F,2*30
void Flarm::parsePGRMZ( const char *pgrmz ) {
	int cs;
	int alt1013_ft;
	int calc_cs=calcNMEACheckSum( pgrmz );
	cs = getNMEACheckSum( pgrmz );
	if( cs != calc_cs ){
		ESP_LOGW(FNAME,"CHECKSUM ERROR: %s; calculcated CS: %d != delivered CS %d", pgrmz, calc_cs, cs );
		return;
	}
	sscanf( pgrmz, "$PGRMZ,%d,F,2",&alt1013_ft );
	timeout = FLARM_TIMEOUT;
	ext_alt_timer = 10;  // Fall back to internal Barometer after 10 seconds
}


int rbOld = -500; // outside normal range

void Flarm::drawClearTriangle( int x, int y, int rb, int dist, int size, int factor ) {
	if( rbOld != -500 ){
		drawTriangle( x,y, rbOld, dist, size, factor, true );
	}
	drawTriangle( x,y, rb, dist, size, factor );
	rbOld = rb;
}

int rbVert = -500;

void Flarm::drawClearVerticalTriangle( int x, int y, int rb, int dist, int size, int factor ) {
	if( rbVert != -500 ){
		drawTriangle( x,y, rbVert, dist, size, factor, true );
	}
	drawTriangle( x,y, rb, dist, size, factor );
	rbVert = rb;
}

void Flarm::drawTriangle( int x, int y, int rb, int dist, int size, int factor, bool erase ) {
	float s = sin( DTR(rb) );
	float c = cos( DTR(rb) );
	int tipx = (int)(x + s*dist );
	int tipy = (int)(y - c*dist );
	float mx =  x + s*(dist+size);
	float my =  y - c*(dist+size);
	int ax = (int)( mx + (size/factor)*c);
	int ay = (int)( my + (size/factor)*s);
	int bx = (int)( mx - (size/factor)*c);
	int by = (int)( my - (size/factor)*s);
	if( erase )
		ucg->setColor( COLOR_BLACK );
	else
		ucg->setColor( COLOR_RED );
	// ESP_LOGI(FNAME,"s: %f c:%f tipx: %d tipy:%d  mx:%2.2f my:%2.2f  ax:%d ay:%d", s,c, tipx, tipy, mx,my, ax,ay);
	ucg->drawTriangle( tipx, tipy, ax,ay, bx, by );
}

void Flarm::drawAirplane( int x, int y, bool fromBehind, bool smallSize ){
	// ESP_LOGI(FNAME,"drawAirplane x:%d y:%d small:%d", x, y, smallSize );
	if( fromBehind ){
		ucg->drawTetragon( x-30,y-2, x-30,y+2, x+30,y+2, x+30,y-2 );
		ucg->drawTetragon( x-2,y-2, x-2,y-10, x+2,y-10, x+2,y-2 );
		ucg->drawTetragon( x-8,y-12, x-8,y-16, x+8,y-16, x+8,y-12 );
		ucg->drawDisc( x,y, 4, UCG_DRAW_ALL );
	}else{
		if( smallSize ){
			ucg->drawTetragon( x-15,y-1, x-15,y+1, x+15,y+1, x+15,y-1 );  // wings
			ucg->drawTetragon( x-1,y+10, x-1,y-3, x+1,y-3, x+1,y+10 ); // fuselage
			ucg->drawTetragon( x-4,y+10, x-4,y+9, x+4,y+9, x+4,y+10 ); // elevator

		}else{
			ucg->drawTetragon( x-30,y-2, x-30,y+2, x+30,y+2, x+30,y-2 );  // wings
			ucg->drawTetragon( x-2,y+25, x-2,y-10, x+2,y-10, x+2,y+25 ); // fuselage
			ucg->drawTetragon( x-8,y+25, x-8,y+21, x+8,y+21, x+8,y+25 ); // elevator
		}
	}
}

void Flarm::initFlarmWarning(){
	ucg->setPrintPos(15, 25 );
	ucg->setFontPosCenter();
	ucg->setColor( COLOR_WHITE );
	ucg->setFont(ucg_font_fub20_hr);
	ucg->printf( PROGMEM"Traffic Alert" );
	ucg->setColor( COLOR_HEADER );
	ucg->setFont(ucg_font_fub11_hr);
	ucg->setPrintPos(130,50);
	ucg->printf(PROGMEM"o'Clock");
	ucg->setPrintPos(130,110);
	ucg->printf(PROGMEM"Distance %s", Units::DistanceUnit() );
	ucg->setPrintPos(130,190);
	ucg->printf(PROGMEM"Vertical %s", Units::AltitudeUnitMeterOrFeet() );

	oldDist = 0;
	oldVertical = 0;
	oldBear = 0;
	// Audio::alarm( true, 30, AUDIO_ALARM_FLARM_1 );
}

void Flarm::drawFlarmWarning(){
	// ESP_LOGI(FNAME,"drawFlarmWarning");
	_tick++;
	if( _tick > 500 ) // age FLARM alarm in case there is no more input  50 per second = 10 sec
		AlarmLevel = 0;
	xSemaphoreTake(spiMutex,portMAX_DELAY );

	if( AlarmLevel == 3 ) { // highest, impact 0-8 seconds
		alarm = AUDIO_ALARM_FLARM_3;
	}
	else if( AlarmLevel == 2 ){
		alarm = AUDIO_ALARM_FLARM_2;
	}
	else if( AlarmLevel == 1 ){ // lowest
		alarm = AUDIO_ALARM_FLARM_1;
	}else{
		alarm = AUDIO_ALARM_OFF;
	}

	if( AlarmLevel != alarmOld ) {
		ucg->setPrintPos(200, 25 );
		ucg->setFontPosCenter();
		ucg->setColor( COLOR_WHITE );
		ucg->setFont(ucg_font_fub20_hr, true);
		ucg->printf( "%d ", AlarmLevel );
		alarmOld = AlarmLevel;
	}
	if( oldDist !=  RelativeDistance ) {
		ucg->setPrintPos(130, 140 );
		ucg->setFontPosCenter();
		ucg->setColor( COLOR_WHITE );
		ucg->setFont(ucg_font_fub25_hr, true);
		char d[32] = "\0";
		int dist = rint(Units::Distance(RelativeDistance)/10)*10;
		sprintf(d,"%d   ",dist);
		ucg->printf( d );
		oldDist = RelativeDistance;
	}
	if( oldVertical !=  RelativeVertical ) {
		ucg->setPrintPos(130, 220 );
		ucg->setFontPosCenter();
		ucg->setColor( COLOR_WHITE );
		ucg->setFont(ucg_font_fub25_hr, true);
		char v[32];
		int vdiff = RelativeVertical;
		if( alt_unit.get() != 0 ){  // then its ft or FL -> feet
			vdiff = rint((vdiff/10)*10);
		}
		sprintf(v,"%d    ",  vdiff );
		ucg->printf( v );
		float relDist =  (float)RelativeDistance;
		if( RelativeBearing < 0 )
			relDist = -relDist;
		float horizontalAngle = RTD( atan2( relDist, (float)RelativeVertical) );
		ESP_LOGI(FNAME,"horizontalAngle: %f  vert:%d", horizontalAngle, RelativeVertical );

		drawClearVerticalTriangle( 70, 220, horizontalAngle, 0, 50, 6 );
		ucg->setColor( COLOR_WHITE );
		drawAirplane( 70, 220, true );
		oldVertical = RelativeVertical;
	}
	if( oldBear != RelativeBearing ){
		ucg->setPrintPos(130, 80 );
		ucg->setFontPosCenter();
		ucg->setColor( COLOR_WHITE );
		ucg->setFont(ucg_font_fub25_hr, true );
		char b[32];
		int quant=15;
		if( RelativeBearing < 0 )
			quant=-15;
		int clock = int((RelativeBearing+quant)/30);
		if( clock <= 0 )
			clock += 12;
		sprintf(b,"  %d  ", clock );
		ucg->printf( b );
		drawClearTriangle( 70,120, RelativeBearing, 0, 50, 4 );
		ucg->setColor( COLOR_WHITE );
		drawAirplane( 70, 120 );
		oldBear = RelativeBearing;
	}

	xSemaphoreGive(spiMutex);
}
