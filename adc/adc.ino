/*
    Atmega8 firmware
    Provides Serial interface for reading ADC data
    Manages power state of ESP-01 (publisher) device
*/

#include <EEPROM.h>

const unsigned long default_sleep_period = 10000; // Sleep 10 seconds by default
const int default_duty_periods = 300;
const int modules_sw_port = 8;

const int eeprom_programmed_sign = 128;
const int eeprom_settings_address = 1;

bool no_auto_sleep = false;

struct Settings {
  unsigned long sleep_period;
  int duty_periods;  
};

Settings settings = {
  default_sleep_period,
  default_duty_periods
};

void writeEEPROMSettings() {  
  EEPROM.put(eeprom_settings_address, settings);
  EEPROM.write(0, eeprom_programmed_sign);
}

void readEEPROMSettings() {
  EEPROM.get(eeprom_settings_address, settings);
}

void setup() {
  pinMode(modules_sw_port, OUTPUT);
  Serial.begin(9600);  
  switchModuleSw(true);

  int programmed = EEPROM.read(0);
  if (programmed != eeprom_programmed_sign) {
    writeEEPROMSettings();
  } else {
    readEEPROMSettings();
  }

  Serial.println("MCU ready");
  Serial.println("sleep_period = " + String(settings.sleep_period));
  Serial.println("duty_periods = " + String(settings.duty_periods));
}

void loop() {  
  for (int i=0; i <= settings.duty_periods; ++i) {
    processSerial();
    delay(100);
  }

  if (!no_auto_sleep) {
    goSleep(0);
  }
  
}

const String op_get_analog = "GETAN";
const String op_ping = "PING0";
const String op_sleep = "SLEEP";
const String op_disable_sleep = "NOSLP";
const String op_enable_sleep = "ENSLP";
const String op_set_sleep_period = "STSLP";
const String op_set_duty_periods = "STDTY";
const String op_no_op = "NO_OP";

void strSplit(const String string, const char divider, String *parts, size_t parts_size) {
  size_t str_len = string.length();
  String tmp = "";  
  size_t parts_index = 0;
  for (int i=0; i < str_len; ++i) {
    if (string[i] == divider) {
      if (tmp.length() && parts_index < parts_size) {
        parts[parts_index] = tmp;
        ++parts_index;
      }      
      tmp = "";      
    } else {
      tmp += string[i];
    }
  }
  if (tmp.length() && parts_index < parts_size) {    
    parts[parts_index] = tmp;
  }     
}

void parseCommand(const String line, String &operation, String *args, size_t parts_size) {  
  if (line.length() < 6) {
    operation = op_no_op;
    return;
  }
  operation = line.substring(0, 5);
  String args_line = line.substring(6);
  strSplit(args_line, '#', args, parts_size);
}

void processSerial() {
  while (Serial.available() > 0) {
    String line = Serial.readStringUntil('\n');
    String operation = "";
    const size_t parts_size = 10;
    String args[parts_size];
    parseCommand(line, operation, args, parts_size);

    if (operation == op_no_op) {
      continue;
    } else if (operation == op_get_analog) {
      processReadAnalog();
    } else if (operation == op_ping) {
      Serial.println("pong");
    } else if (operation == op_sleep) {
      unsigned long period =  atol(args[0].c_str());
      goSleep(period);
    } else if (operation == op_disable_sleep) {
      no_auto_sleep = true;
      Serial.println("done");
    } else if (operation == op_enable_sleep) {
      no_auto_sleep = false;
      Serial.println("done");
    } else if (operation == op_set_sleep_period) {
      unsigned long period =  atol(args[0].c_str());
      if (period <= 0) {
        Serial.println("invalid period");
        continue;
      }
      settings.sleep_period = period;
      writeEEPROMSettings();
      Serial.println("done");
    } else if (operation == op_set_duty_periods) {
      int period = args[0].toInt();
      if (period <= 0) {
        Serial.println("invalid period");
        continue;
      }
      settings.duty_periods = period;
      writeEEPROMSettings();
      Serial.println("done");
    }    
  }
}

void processReadAnalog() {
  reportAnalogPort(A3);
  reportAnalogPort(A2);
  reportAnalogPort(A1);
  reportAnalogPort(A0);
}

void reportAnalogPort(int port) {
  Serial.print("analog#");
  Serial.print(port);
  Serial.print("#");
  Serial.println(analogRead(port));
}

void goSleep(unsigned long period) {
  unsigned long duaration_ms = settings.sleep_period;
  if (period != 0) {
    duaration_ms = period;
  }
  switchModuleSw(false);
  Serial.println("start_sleep#");
  delay(duaration_ms);
  switchModuleSw(true);
  Serial.println("end_sleep#");
  Serial.flush();
}

void switchModuleSw(bool enabled) {
  digitalWrite(modules_sw_port, !enabled);
}
