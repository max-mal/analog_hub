const String op_get_analog = "GETAN";
const String op_ping = "PING0";
const String op_sleep = "SLEEP";
const String op_disable_sleep = "NOSLP";
const String op_enable_sleep = "ENSLP";
const String op_set_sleep_period = "STSLP";
const String op_set_duty_periods = "STDTY";
const String op_no_op = "NO_OP";

void serialExecCmd(const String cmd, const String args[], const size_t args_count) {
  Serial.flush();
  Serial.print(cmd);
  Serial.print('#');
  for (int i=0; i < args_count; ++i) {
    Serial.print(args[i]);
    Serial.print('#');
  }
  Serial.println();
}

std::pair<int, int> serialReadAnalogItem() {
  String line = Serial.readStringUntil('\n');
  const size_t parts_size = 10;
  String parts[parts_size];
  strSplit(line, '#', parts, parts_size);

  if (parts[0] != "analog") {
    return {-1, 0};
  }
  
  const int pin = parts[1].toInt();
  const int value = parts[2].toInt();
  return {pin, value};
}

std::vector<std::pair<int, int>> serialReadAnalogData() {
  std::vector<std::pair<int, int>> values;  
  
  serialExecCmd(op_get_analog, {}, 0);

  for (int i=0; i != 4; ++i) {
    auto item = serialReadAnalogItem();
    if (item.first == -1) {
      continue;
    }

    values.push_back(item);
  }

  return values;
}


void serialGoSleep() {
  serialExecCmd(op_sleep, {}, 0);
}

bool serialCheckConnection() {
  serialExecCmd(op_ping, {}, 0);
  String res = Serial.readStringUntil('\n');
  res.trim();
  if (res == "pong") {
    return true;
  }
  
  return false;
}

bool serialToggleSleep(bool value) {
  serialExecCmd(value? op_enable_sleep : op_disable_sleep, {}, 0);
  String response = Serial.readStringUntil('\n');
  response.trim();

  return response.equals("done");
}

bool serialSetSleepPeriod(int period) {  
  String args[1] = {String(period)};
  serialExecCmd(op_set_sleep_period, args, 1);
  String response = Serial.readStringUntil('\n');
  response.trim();

  return response.equals("done");
}

bool serialSetDutyPeriods(int periods) {
  String args[1] = {String(periods)};
  serialExecCmd(op_set_duty_periods, args, 1);
  String response = Serial.readStringUntil('\n');
  response.trim();

  return response.equals("done");
}
