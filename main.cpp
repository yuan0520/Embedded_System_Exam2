#include "mbed.h"
#include "mbed_rpc.h"
#include "uLCD_4DGL.h"
#include "stm32l475e_iot01_accelero.h"
#include "math.h"
#include <string>

#include "accelerometer_handler.h"
#include "config.h"
#include "magic_wand_model_data.h"

#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/micro/kernels/micro_ops.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/version.h"

#include "MQTTNetwork.h"
#include "MQTTmbed.h"
#include "MQTTClient.h"

#define RPC_LOOP 0
#define GESTURE_UI_MODE 1
#define TILT_ANGLE_DETECTION_MODE 2

WiFiInterface *wifi;
int message_num = 0;
volatile int arrivedcount = 0;
volatile bool closed = false;
int blink_time = 10;

const char* topic1 = "angle_sel";
const char* topic2 = "value";
// const char* topic2 = "angle_det";

uLCD_4DGL uLCD(D1, D0, D2);
// InterruptIn user_btn(USER_BUTTON);
BufferedSerial pc(USBTX, USBRX);

EventQueue menu_queue(32 * EVENTS_EVENT_SIZE);
Thread menu_t;
EventQueue acc_queue(32 * EVENTS_EVENT_SIZE); // queue for accelero
Thread acc_t;
EventQueue gesture_queue(32 * EVENTS_EVENT_SIZE); // queue for gesture
Thread gesture_t;
EventQueue angle_detection_queue(32 * EVENTS_EVENT_SIZE); // queue for angle_detection
Thread angle_detection_t;
Thread mqtt_thread(osPriorityHigh);
EventQueue mqtt_queue;
EventQueue LED_queue(32 * EVENTS_EVENT_SIZE);
Thread LED_t;

int mode = RPC_LOOP;
int angle = 15; // initial value of angle is 15
int angle_sel = 15;
double angle_det = 0.0;

int seq_num = 0;
int ges_ID[50];
int feature[50];
int id_now = 0;
int feature_now = 0;


void menu();
void menu_selected();

void menu(){
    uLCD.background_color(WHITE);
    uLCD.cls();
    uLCD.textbackground_color(WHITE);
    uLCD.color(BLACK);
    uLCD.locate(1, 2);
    uLCD.printf("\n15\n\n");
    uLCD.locate(1, 4);
    uLCD.printf("\n30\n\n");
    uLCD.locate(1, 6);
    uLCD.printf("\n45\n\n");

    if(angle == 15){
        uLCD.color(BLUE);
        uLCD.locate(1, 2);
        uLCD.printf("\n15\n\n");
    }
    else if(angle == 30){
        uLCD.color(BLUE);
        uLCD.locate(1, 4);
        uLCD.printf("\n30\n\n");
    }
    else{
        uLCD.color(BLUE);
        uLCD.locate(1, 6);
        uLCD.printf("\n45\n\n");
    }
}

void menu_selected(){
    uLCD.background_color(WHITE);
    uLCD.cls();
    uLCD.textbackground_color(WHITE);

    uLCD.color(BLACK);
    uLCD.locate(4, 4);
    uLCD.printf("\nID: %d\n", id_now);
}

void menu_angle_det(){
    uLCD.background_color(BLACK);
    uLCD.cls();
    uLCD.textbackground_color(BLACK);

    uLCD.color(WHITE);
    uLCD.locate(4, 4);
    uLCD.printf("\nangle_det: %.2f\n", angle_det);
}

void messageArrived_val(MQTT::MessageData& md) {
    MQTT::Message &message = md.message;
    char msg[300];
    sprintf(msg, "Message arrived: QoS%d, retained %d, dup %d, packetID %d\r\n", message.qos, message.retained, message.dup, message.id);
    printf(msg);
    ThisThread::sleep_for(1000ms);
    char payload[300];
    sprintf(payload, "Payload %.*s\r\n", message.payloadlen, (char*)message.payload);
    printf(payload);
    ++arrivedcount;
}

void publish_message_val(MQTT::Client<MQTTNetwork, Countdown>* client_val) {
    // message_num++;
    MQTT::Message message;
    char buff[100];
    for(int i=0; i<10; i++){
        sprintf(buff, "gesture %d:%d, feature %d:%d\n",i, ges_ID[i], i, feature[i]);
    }
    message.qos = MQTT::QOS0;
    message.retained = false;
    message.dup = false;
    message.payload = (void*) buff;
    message.payloadlen = strlen(buff) + 1;
    int rc = client_val->publish(topic2, message);
}

void messageArrived_select_angle(MQTT::MessageData& md) {
    MQTT::Message &message = md.message;
    char msg[300];
    sprintf(msg, "Message arrived: QoS%d, retained %d, dup %d, packetID %d\r\n", message.qos, message.retained, message.dup, message.id);
    printf(msg);
    ThisThread::sleep_for(1000ms);
    char payload[300];
    sprintf(payload, "Payload %.*s\r\n", message.payloadlen, (char*)message.payload);
    printf(payload);
    ++arrivedcount;
}

void publish_message_sel(MQTT::Client<MQTTNetwork, Countdown>* client_sel) {
    // message_num++;
    MQTT::Message message;
    char buff[100];
    sprintf(buff, "GestureID:%d, Feature value:%d, Sequence number:%d.", id_now, feature_now, seq_num);
    message.qos = MQTT::QOS0;
    message.retained = false;
    message.dup = false;
    message.payload = (void*) buff;
    message.payloadlen = strlen(buff) + 1;
    int rc = client_sel->publish(topic1, message);
}


void close_mqtt() {
    closed = true;
}

constexpr int kTensorArenaSize = 60 * 1024;
uint8_t tensor_arena[kTensorArenaSize];

// Return the result of the last prediction
int PredictGesture(float* output) {
  // How many times the most recent gesture has been matched in a row
  static int continuous_count = 0;
  // The result of the last prediction
  static int last_predict = -1;

  // Find whichever output has a probability > 0.8 (they sum to 1)
  int this_predict = -1;
  for (int i = 0; i < label_num; i++) {
    if (output[i] > 0.8) this_predict = i;
  }

  // No gesture was detected above the threshold
  if (this_predict == -1) {
    continuous_count = 0;
    last_predict = label_num;
    return label_num;
  }

  if (last_predict == this_predict) {
    continuous_count += 1;
  } else {
    continuous_count = 0;
  }
  last_predict = this_predict;

  // If we haven't yet had enough consecutive matches for this gesture,
  // report a negative result
  if (continuous_count < config.consecutiveInferenceThresholds[this_predict]) {
    return label_num;
  }
  // Otherwise, we've seen a positive result, so clear all our variables
  // and report it
  continuous_count = 0;
  last_predict = -1;

  return this_predict;
}

void gesture(Arguments *in, Reply *out);
RPCFunction gesture_RPC(&gesture, "gesture");
void gesture_UI_mode();


void gesture(Arguments *in, Reply *out){
  // printf("Call gesture RPC function\n");
  gesture_t.start(callback(&gesture_queue, &EventQueue::dispatch_forever));
  gesture_queue.call(gesture_UI_mode);
}

void gesture_UI_mode(){
  mode = GESTURE_UI_MODE;
  printf("Enter ACC mode.\n\n");

  NetworkInterface* net = wifi;
  MQTTNetwork mqttNetwork(net);
  MQTT::Client<MQTTNetwork, Countdown> client_sel(mqttNetwork);

  //TODO: revise host to your IP
  const char* host = "172.20.10.10";
  printf("Connecting to TCP network...\r\n");

  SocketAddress sockAddr;
  sockAddr.set_ip_address(host);
  sockAddr.set_port(1883);

  printf("address is %s/%d\r\n", (sockAddr.get_ip_address() ? sockAddr.get_ip_address() : "None"),  (sockAddr.get_port() ? sockAddr.get_port() : 0) ); //check setting

  int rc = mqttNetwork.connect(sockAddr);
  printf("rc = %d\n", rc);
  if (rc != 0) {
      printf("Connection error.");
      // return -1;
  }
  printf("client_sel Successfully connected!\r\n");

  MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
  data.MQTTVersion = 3;
  data.clientID.cstring = "Mbed";

  if ((rc = client_sel.connect(data)) != 0){
      printf("Fail to connect MQTT\r\n");
  }
  if (client_sel.subscribe(topic1, MQTT::QOS0, messageArrived_select_angle) != 0){
      printf("Fail to subscribe\r\n");
  }

  ThisThread::sleep_for(2000ms);

  int16_t ref_XYZ[3] = {0};
  int16_t real_XYZ[3] = {0};
  double mag_A;
  double mag_B;
  double cos_value;
  double rad;

  printf("Start to measure reference XYZ value.\n");
  ThisThread::sleep_for(3000ms);

  BSP_ACCELERO_AccGetXYZ(ref_XYZ);
  printf("Finish measuring reference XYZ value.\n");
  printf("Reference XYZ value: %d, %d, %d\n\n", ref_XYZ[0], ref_XYZ[1], ref_XYZ[2]);

  ThisThread::sleep_for(2000ms);

  printf("Start to detect gesture and capture feature.\n\n");

  // Whether we should clear the buffer next time 
  bool should_clear_buffer = false;
  bool got_data = false;

  // The gesture index of the prediction
  int gesture_index;

  // Set up logging.
  static tflite::MicroErrorReporter micro_error_reporter;
  tflite::ErrorReporter* error_reporter = &micro_error_reporter;

  // Map the model into a usable data structure. This doesn't involve any
  // copying or parsing, it's a very lightweight operation.
  const tflite::Model* model = tflite::GetModel(g_magic_wand_model_data);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    error_reporter->Report(
        "Model provided is schema version %d not equal "
        "to supported version %d.",
        model->version(), TFLITE_SCHEMA_VERSION);
    // return -1;
  }

  // Pull in only the operation implementations we need.
  // This relies on a complete list of all the ops needed by this graph.
  // An easier approach is to just use the AllOpsResolver, but this will
  // incur some penalty in code space for op implementations that are not
  // needed by this graph.
  static tflite::MicroOpResolver<6> micro_op_resolver;
  micro_op_resolver.AddBuiltin(
      tflite::BuiltinOperator_DEPTHWISE_CONV_2D,
      tflite::ops::micro::Register_DEPTHWISE_CONV_2D());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_MAX_POOL_2D,
                               tflite::ops::micro::Register_MAX_POOL_2D());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_CONV_2D,
                               tflite::ops::micro::Register_CONV_2D());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_FULLY_CONNECTED,
                               tflite::ops::micro::Register_FULLY_CONNECTED());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_SOFTMAX,
                               tflite::ops::micro::Register_SOFTMAX());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_RESHAPE,
                               tflite::ops::micro::Register_RESHAPE(), 1);

  // Build an interpreter to run the model with
  static tflite::MicroInterpreter static_interpreter(
      model, micro_op_resolver, tensor_arena, kTensorArenaSize, error_reporter);
  tflite::MicroInterpreter* interpreter = &static_interpreter;

  // Allocate memory from the tensor_arena for the model's tensors
  interpreter->AllocateTensors();

  // Obtain pointer to the model's input tensor
  TfLiteTensor* model_input = interpreter->input(0);
  if ((model_input->dims->size != 4) || (model_input->dims->data[0] != 1) ||
      (model_input->dims->data[1] != config.seq_length) ||
      (model_input->dims->data[2] != kChannelNumber) ||
      (model_input->type != kTfLiteFloat32)) {
    error_reporter->Report("Bad input tensor parameters in model");
    // return -1;
  }

  int input_length = model_input->bytes / sizeof(float);

  TfLiteStatus setup_status = SetupAccelerometer(error_reporter);
  if (setup_status != kTfLiteOk) {
    error_reporter->Report("Set up failed\n");
    // return -1;
  }

  error_reporter->Report("Set up successful...\n");

  while (mode == GESTURE_UI_MODE) {

    // Attempt to read new data from the accelerometer
    got_data = ReadAccelerometer(error_reporter, model_input->data.f,
                                 input_length, should_clear_buffer);

    // If there was no new data,
    // don't try to clear the buffer again and wait until next time
    if (!got_data) {
      should_clear_buffer = false;
      continue;
    }

    // Run inference, and report any error
    TfLiteStatus invoke_status = interpreter->Invoke();
    if (invoke_status != kTfLiteOk) {
      error_reporter->Report("Invoke failed on index: %d\n", begin_index);
      continue;
    }

    // Analyze the results to obtain a prediction
    gesture_index = PredictGesture(interpreter->output(0)->data.f);

    // Clear the buffer next time we read data
    should_clear_buffer = gesture_index < label_num;

    // Produce an output
    if (gesture_index < label_num) {
      error_reporter->Report(config.output_message[gesture_index]);
    }

    // judge gesture and select specific angle
    if(gesture_index == 0){
      printf("Sequence number: %d\n", seq_num);
      printf("Gesture 0: Ring\n");
      id_now = 0;
      ges_ID[seq_num] = id_now;

      BSP_ACCELERO_AccGetXYZ(real_XYZ);
      // printf("detected XYZ value: %d, %d, %d", real_XYZ[0], real_XYZ[1], real_XYZ[2]);
      mag_A = sqrt(ref_XYZ[0]*ref_XYZ[0] + ref_XYZ[1]*ref_XYZ[1] + ref_XYZ[2]*ref_XYZ[2]);
      mag_B = sqrt(real_XYZ[0]*real_XYZ[0] + real_XYZ[1]*real_XYZ[1] + real_XYZ[2]*real_XYZ[2]);
      cos_value = ((ref_XYZ[0]*real_XYZ[0] + ref_XYZ[1]*real_XYZ[1] + ref_XYZ[2]*real_XYZ[2])/(mag_A)/(mag_B));
      rad = acos(cos_value);
      angle_det = 180.0 * rad/M_PI;
      printf("  angle changed = %.2f\r\n", abs(angle_det));
      // menu_queue.call(menu_angle_det);

      if (abs(angle_det) > angle_sel) {
        feature_now = 1;
        feature[seq_num] = feature_now;
        printf("feature value = 1\n\n");
      }
      else{
        feature_now = 0;
        feature[seq_num] = feature_now;
        printf("feature value = 0\n\n");
      }
      mqtt_queue.call(&publish_message_sel, &client_sel);

      seq_num++;

      // angle = 15;
      // angle_sel = angle;
      menu_queue.call(menu_selected);
      ThisThread::sleep_for(500ms);
    }
    else if(gesture_index == 1){
      printf("Sequence number: %d\n", seq_num);
      printf("Gesture 1: Slope\n");
      id_now = 1;
      ges_ID[seq_num] = id_now;

      BSP_ACCELERO_AccGetXYZ(real_XYZ);
      // printf("detected XYZ value: %d, %d, %d", real_XYZ[0], real_XYZ[1], real_XYZ[2]);
      mag_A = sqrt(ref_XYZ[0]*ref_XYZ[0] + ref_XYZ[1]*ref_XYZ[1] + ref_XYZ[2]*ref_XYZ[2]);
      mag_B = sqrt(real_XYZ[0]*real_XYZ[0] + real_XYZ[1]*real_XYZ[1] + real_XYZ[2]*real_XYZ[2]);
      cos_value = ((ref_XYZ[0]*real_XYZ[0] + ref_XYZ[1]*real_XYZ[1] + ref_XYZ[2]*real_XYZ[2])/(mag_A)/(mag_B));
      rad = acos(cos_value);
      angle_det = 180.0 * rad/M_PI;
      printf("  angle changed = %.2f\r\n", abs(angle_det));
      // menu_queue.call(menu_angle_det);

      if (abs(angle_det) > angle_sel) {
        feature_now = 1;
        feature[seq_num] = feature_now;
        printf("feature value = 1\n\n");
      }
      else{
        feature_now = 0;
        feature[seq_num] = feature_now;
        printf("feature value = 0\n\n");
      }
      mqtt_queue.call(&publish_message_sel, &client_sel);

      seq_num++;

      // angle = 30;
      // angle_sel = angle;
      menu_queue.call(menu_selected);
      ThisThread::sleep_for(500ms);
    }
    else if(gesture_index == 2){
      printf("Sequence number: %d\n", seq_num);
      printf("Gesture 2: Line\n");
      id_now = 2;
      ges_ID[seq_num] = id_now;
      

      BSP_ACCELERO_AccGetXYZ(real_XYZ);
      // printf("detected XYZ value: %d, %d, %d", real_XYZ[0], real_XYZ[1], real_XYZ[2]);
      mag_A = sqrt(ref_XYZ[0]*ref_XYZ[0] + ref_XYZ[1]*ref_XYZ[1] + ref_XYZ[2]*ref_XYZ[2]);
      mag_B = sqrt(real_XYZ[0]*real_XYZ[0] + real_XYZ[1]*real_XYZ[1] + real_XYZ[2]*real_XYZ[2]);
      cos_value = ((ref_XYZ[0]*real_XYZ[0] + ref_XYZ[1]*real_XYZ[1] + ref_XYZ[2]*real_XYZ[2])/(mag_A)/(mag_B));
      rad = acos(cos_value);
      angle_det = 180.0 * rad/M_PI;
      printf("angle changed = %.2f\r\n", abs(angle_det));
      // menu_queue.call(menu_angle_det);

      if (abs(angle_det) > angle_sel) {
        feature_now = 1;
        feature[seq_num] = feature_now;
        printf("feature value = 1\n\n");
      }
      else{
        feature_now = 0;
        feature[seq_num] = feature_now;
        printf("feature value = 0\n\n");
      }
      mqtt_queue.call(&publish_message_sel, &client_sel);

      seq_num++;
      // angle = 45;
      // angle_sel = angle;
      menu_queue.call(menu_selected);
      ThisThread::sleep_for(500ms);
    }
    
  }
}

void get_value(Arguments *in, Reply *out);
RPCFunction get_value_RPC(&get_value, "get_value");

void get_value(Arguments *in, Reply *out){

  NetworkInterface* net = wifi;
  MQTTNetwork mqttNetwork(net);
  MQTT::Client<MQTTNetwork, Countdown> client_val(mqttNetwork);

  //TODO: revise host to your IP
  const char* host = "172.20.10.10";
  // printf("Connecting to TCP network...\r\n");

  SocketAddress sockAddr;
  sockAddr.set_ip_address(host);
  sockAddr.set_port(1883);

  // printf("address is %s/%d\r\n", (sockAddr.get_ip_address() ? sockAddr.get_ip_address() : "None"),  (sockAddr.get_port() ? sockAddr.get_port() : 0) ); //check setting

  int rc = mqttNetwork.connect(sockAddr);
  // printf("rc = %d\n", rc);
  if (rc != 0) {
      printf("Connection error.");
      // return -1;
  }
  // printf("client_val Successfully connected!\r\n");

  MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
  data.MQTTVersion = 3;
  data.clientID.cstring = "value";

  if ((rc = client_val.connect(data)) != 0){
      printf("Fail to connect MQTT\r\n");
  }
  if (client_val.subscribe(topic2, MQTT::QOS0, messageArrived_val) != 0){
      printf("Fail to subscribe\r\n");
  }

  mqtt_queue.call(&publish_message_val, &client_val);

  for(int i=0; i<10; i++){
    printf("gesture %d:%d, feature %d:%d\n",i, ges_ID[i], i, feature[i]);
  }
}

void back_finished(Arguments *in, Reply *out);
RPCFunction back_RPC2(&back_finished, "back_finished");

void back_finished(Arguments *in, Reply *out){
  mode = RPC_LOOP;
  printf("\nBack to RPC loop.\n\n");
  
  ThisThread::sleep_for(1000ms);
  seq_num = 0;
  id_now = 0;
  feature_now = 0;
  message_num = 0;
}




int main(){
    memset(ges_ID, -1, sizeof(ges_ID));
    memset(feature, -1, sizeof(feature));
    // init uLCD
    menu_t.start(callback(&menu_queue, &EventQueue::dispatch_forever));
    uLCD.background_color(WHITE);
    uLCD.cls();
    uLCD.textbackground_color(WHITE);
    // menu();

    // init accelero
    printf("Start accelerometer init\n");
    BSP_ACCELERO_Init();
    acc_t.start(callback(&menu_queue, &EventQueue::dispatch_forever));

    LED_t.start(callback(&LED_queue, &EventQueue::dispatch_forever));

    // mqtt_thread.start(callback(&mqtt_queue, &EventQueue::dispatch_forever));

    wifi = WiFiInterface::get_default_instance();
    if (!wifi) {
        printf("ERROR: No WiFiInterface found.\r\n");
        // return -1;
    }

    printf("\nConnecting to %s...\r\n", MBED_CONF_APP_WIFI_SSID);
    int ret = wifi->connect(MBED_CONF_APP_WIFI_SSID, MBED_CONF_APP_WIFI_PASSWORD, NSAPI_SECURITY_WPA_WPA2);
    if (ret != 0) {
        printf("\nConnection error: %d\r\n", ret);
        // return -1;
    }

    mqtt_thread.start(callback(&mqtt_queue, &EventQueue::dispatch_forever));

    //The mbed RPC classes are now wrapped to create an RPC enabled version - see RpcClasses.h so don't add to base class
    // receive commands, and send back the responses
    char buf[256], outbuf[256];

    FILE *devin = fdopen(&pc, "r");
    FILE *devout = fdopen(&pc, "w");

    while(1) {
        memset(buf, 0, 256);
        for (int i = 0; ; i++) {
            char recv = fgetc(devin);
            if (recv == '\n') {
                printf("\r\n");
                break;
            }
            buf[i] = fputc(recv, devout);
        }
        //Call the static call method on the RPC class
        RPC::call(buf, outbuf);
        printf("%s\r\n", outbuf);
    }
}