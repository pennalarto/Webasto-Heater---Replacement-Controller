//*********************************************************************************************
// DIY Webasto Controller
// Board:  Adafruit Feather M0
// Code based on a Webasto Shower Controller by David McLuckie
// https://davidmcluckie.com/arduino-webasto-shower-project/ 
// His project was based on the work of Mael Poureau
// https://maelpoureau.com/webasto_shower/
//
// I've changed it from a Shower to a general purpose controller which tries to regulate the 
// Temperature to a pre-defined target by adjusting the Fueling and Combustion fan
// I've converted the code to run on an Adafruit Huzzah32 (ESP32) and designed a drop-in
// replacement PCB for Webasto ThermoTop C & E Heaters.
// The wiring harness is similar to the original, except:
// 6 Pin Connector - Pin:  [Changes indicated by * ]
// 1 - Clock (+12V = Heater On)/(0V = Heater Off)
// 2*- ESP32 Serial Out (Not used)
// 3*- Exhaust Thermistor (100k NTC between pin 3 & Gnd)
// 4*- ESP Analog Input 03 (could be configured as a 3.3V Output)
// 5*- Water Thermistor (100k NTC between pin 3 & Gnd) [See Note]
// 6 - Fuel Dosing Pump
// 
// [Note] The PCB has holes for the original Thermistor which you can salvage from an old unit
// It has a different 25C Resistance around 4.7k.  You will need to change R12 to 4.7k
// and in get_webasto_temp, change "Nominal resistance at 25 ºC" from 100000 to 4700
//
//
// Simon Rafferty SimonSFX@Outlook.com 2020
//*********************************************************************************************

#include <math.h> // needed to perform some calculations
//#include <SAMD21turboPWM.h>

//Heater Config 
int heater_min = 60; // Increase fuel if below
int heater_target = 70; // degrees C Decrease fuel if above
int water_warning = 75;// degrees C
int water_overheat = 85;// degrees C

//Fuel Mixture 
//Prime
float prime_low_temp = 0;
float prime_high_temp = 20;

float prime_fan_speed = 15;
float prime_low_temp_fuelrate = 3.5;
float prime_high_temp_fuelrate = 2.0;

//Inital
float start_fan_speed = 40;
float start_fuel = 1;

int full_power_increment_time = 30; //seconds


//Throttling_Power Quiet Mode
float throttling_high_fuel = 1.8;
float throttling_high_fan = 90;
float throttling_steady_fuel = 1.3;
float throttling_steady_fan = 65;
float throttling_low_fuel = 0.83;
float throttling_low_fan = 50;
float throttling_idle_fuel = 0.5; //Just enough to keep it alight
float throttling_idle_fan = 30; 


//Pin Connections
int fuel_pump_pin = 11;
int glow_plug_pin = 5;
int burn_fan_pin = 10;
int water_pump_pin = 9;
int water_temp_pin = A1;
int exhaust_temp_pin = A2;
int lambda_pin = A3;
int push_pin = A0;
//Pin Connections

//Hardware Config
int pump_size = 60; //22,30,60 
//bool high_temp_exhaust_sensor = false;
//Hardware Config

//Temperature Filtering
#define filterSamples   13              // filterSamples should  be an odd number, no smaller than 3
float rawDataWater, smoothDataWater;  // variables for sensor1 data
float rawDataExhaust, smoothDataExhaust;  // variables for sensor2 data

float WaterSmoothArray [filterSamples];   // array for holding raw sensor values for sensor1 
float ExhaustSmoothArray [filterSamples];   // array for holding raw sensor values for sensor2 

float Last_Exh_T = 0;
float Last_Wat_T = 0;
float Last_Mute_T = 0;
int GWTLast_Sec;
int Last_TSec;
boolean EX_Mute = false;
float Last_Temp = 0;
float Max_Change_Per_Sec = 2;  //Used to slow down changes in temperature to remove spikes
//Flame Sensor workings
float Flame_Diff = 0; 
float Flame_Threshold = 1.000;



//Serial Settings
String message = "Off";
//bool pushed;
//bool long_press;
bool heater_on;
bool debug_glow_plug_on = 2;
int debug_water_percent_map = 999;

//Varaiables
int Incidents = 0; //Things that have gone wrong during combustion
int Ignition_Failures = 0;
float fan_speed; // percent
float water_pump_speed; // percent
float fuel_need; // percent
int glow_time; // seconds
float water_temp; // degres C
float exhaust_temp; // degres C
float exhaust_temp_sec[11]; // array of last 10 sec water temp, degres C
int water_temp_sec[181];
int glow_left = 0;
int last_glow_value = 0;
bool burn = false;
bool webasto_fail = false;
int Ignit_Fail = 0;
int seconds;

bool lean_burn;
int delayed_period = 0;
unsigned long water_pump_started_on;
int water_pump_started = 0;
long glowing_on = 0;
int burn_mode = 0;



//PWM properties
//TurboPWM pwm; //Turbo PWM for SAMD21

const int glow_channel = 0;
const int water_channel = 1;
const int air_channel = 2;

void setup() {
  pinMode(glow_plug_pin, OUTPUT);
  pinMode(fuel_pump_pin, OUTPUT);
  pinMode(burn_fan_pin, OUTPUT);
  pinMode(water_pump_pin, OUTPUT);
  pinMode(water_temp_pin, INPUT);
  pinMode(exhaust_temp_pin, INPUT);
  pinMode(push_pin, INPUT);
  pinMode(lambda_pin, INPUT);

  
  
  //PWM Works differently on ESP32
  //TCCR1B = TCCR1B & 0b11111000 | 0x01;  // magic Fast PWM parameter
  // configure PWM
  //ledcSetup(glow_channel, freq, resolution);  
  // attach the channel to the GPIO2 to be controlled
  //ledcAttachPin(glow_plug_pin, glow_channel);

  //ledcSetup(water_channel, freq, resolution);  
  // attach the channel to the GPIO2 to be controlled
  //ledcAttachPin(water_pump_pin, water_channel);

  //ledcSetup(air_channel, freq, resolution);  
  // attach the channel to the GPIO2 to be controlled
  //ledcAttachPin(burn_fan_pin, air_channel);

  //Setup command parser callback functions
  //cmdCallback.addCmd("T", &Serial_Turbo_Mode); //Expects "T 0\n" or "T 1\n"
  //cmdCallback.addCmd("P", &Serial_Purge_Mode); //Expects "P 0\n" or "P 1\n"
  //pwm.setClockDivider(3, false); //This sets a frequency of 15,625Hz at 1024 prescale
  //pwm.timer([0, 1, 2], [1, 2, 4, 8, 16, 64, 256, 1024], [2-MaxSteps], [true, false])
  //pwm.timer(0, 1024, 1000, true);  //Timer=0, Prescaler=1024, Steps=1000, Single Slope PWM

  
  analogWrite(water_pump_pin, 100); //Run water pump on startup for a few seconds
  //Pulse Burn fan - to test & indicate startup
  fan_speed = 70;
  burn_fan();
  delay(1000);
  fan_speed = 0;
  burn_fan();
  delay(1000);
  fan_speed = 70;
  burn_fan();
  delay(1000);
  fan_speed = 0;
  burn_fan();
  delay(3000);
  

  //pwm.enable(0, true);  //Start Timer 0
  
  Serial.begin(115200);

  
}

void loop() { // runs over and over again, calling the functions one by one
  temp_data();
  control();
  webasto();
  // Check for new char on serial and call function if command was entered
  //cmdCallback.updateCmdProcessing(&myParser, &myBuffer, &Serial1);

}
