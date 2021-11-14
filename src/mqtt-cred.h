/* not to be stored publicly */
#define MQTT_LOG_LOCAL

#ifdef MQTT_LOG_LOCAL
#define MQTT_LOG "pottendo-pi30-phono"
#else
#define MQTT_LOG "node02.myqtthub.com"
#endif

#define MQTT_LOG_USER "XXXuser"
#define MQTT_LOG_PW "XXXPW"

#define MQTT_CRED