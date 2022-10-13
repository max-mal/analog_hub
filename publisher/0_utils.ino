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