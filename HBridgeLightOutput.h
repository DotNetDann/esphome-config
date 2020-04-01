#include "esphome.h"
// This is a bit flickery however it works.
// Use the White value to set the color

//TODO: Pass in pin parameters

class HBridgeLightOutput : public PollingComponent, public LightOutput {

// typedef for properties of each sw pwm pin
typedef struct pwmPins {
  int pin1;
  int pin2;
  int pwm1Value;
  int pwm2Value;
  int pwmTickCount;
} pwmPin;

pwmPin myPWMpin;

 public:
 
  HBridgeLightOutput() : PollingComponent(1) {}
 
  void setup() override {
    // This will be called once to set up the component
    // think of it as the setup() call in Arduino


    myPWMpin.pin1 = 12; //GPIO12;
    myPWMpin.pin2 = 14; //GPIO14;
    myPWMpin.pwm1Value = 0; // max 10 (not great but still has some dimming)
    myPWMpin.pwm2Value = 0; // max 10 (not great but still has some dimming)
    myPWMpin.pwmTickCount = 0;

    // unlike analogWrite(), this is necessary
    pinMode(myPWMpin.pin1, OUTPUT);
    pinMode(myPWMpin.pin2, OUTPUT);

    //pinMode(13, OUTPUT); // Debug pin
    ESP_LOGD("HBridgeLED", "Setup Done!");
  }
  
  void update() override {
    // This will be called very often after setup time.
    // think of it as the loop() call in Arduino
    handlePWM();
  }
  
  void handlePWM() {
    // This method runs around 60 times per second.
    // We cannot do the PWM outselfs so we are reliant on the hardware PWM
    
    if (myPWMpin.pwmTickCount == 0) { // First LED Direction
      analogWrite(myPWMpin.pin2, 0);
      analogWrite(myPWMpin.pin1, myPWMpin.pwm1Value);
      myPWMpin.pwmTickCount = 1;
    } else {                          // Second LED Direction
      analogWrite(myPWMpin.pin1, 0);
      analogWrite(myPWMpin.pin2, myPWMpin.pwm2Value);
      myPWMpin.pwmTickCount = 0;
    }
  }
  
  LightTraits get_traits() override {
    // return the traits this light supports
    auto traits = LightTraits();
    traits.set_supports_brightness(true); // Dimming
    traits.set_supports_rgb(false);
    traits.set_supports_rgb_white_value(true);
    traits.set_supports_color_temperature(false);
    return traits;
  }

  void write_state(light::LightState *state) override {

    //float cwhite, wwhite;
    //state->current_values_as_cwww(&cwhite, &wwhite);
    //float red2, green2, blue2, cwhite2, wwhite2;
    //state->current_values_as_rgbww(&red2, &green2, &blue2, &cwhite2, &wwhite2);

    float bright;
    state->current_values_as_brightness(&bright);

    state->set_gamma_correct(0);
    float red, green, blue, white;
    state->current_values_as_rgbw(&red, &green, &blue, &white);


    if (white > 0.55) {
      myPWMpin.pwm1Value = (bright * (1-white) * 1024);
      myPWMpin.pwm2Value = (bright * 1024);
    } else if (white < 0.45) {
      myPWMpin.pwm1Value = (bright * 1024);
      myPWMpin.pwm2Value = (bright * (white) * 1024);
    } else {
      myPWMpin.pwm1Value = (bright * 1024);
      myPWMpin.pwm2Value = (bright * 1024);
    }

    // So 0 will be even
    //white = white - 0.5;
    //myPWMpin.pwm1Value = (int)(bright * (white+0.5) * 1024);
    //myPWMpin.pwm2Value = (bright * (0.5-white) * 1024);


    ESP_LOGD("HBridgeLightOutput", "LED 1: %d       LED 2: %d", myPWMpin.pwm1Value, myPWMpin.pwm2Value);
    //ESP_LOGD("HBridgeLightOutput", "Changed State: %d|%d from %f|%f", myPWMpin.pwm1Value, myPWMpin.pwm2Value, bright, white);
  }
};





// ---------------------------------------
// ---- Example Config -------------------
// ---------------------------------------
//
//light:
//  - platform: custom
//    lambda: |-
//      auto light_out = new HBridgeLightOutput();
//      App.register_component(light_out);
//      return {light_out};
//      
//    lights:
//      name: "Icicle Lights"
//      effects:
//      - flicker:
//      - strobe:
//          name: Strobe
//          colors:
//            - state: True
//              white: 0%
//              duration: 500ms
//            - state: True
//              white: 100%
//              duration: 500ms
//      - strobe:
//          name: Blink
//          colors:
//            - state: True
//              white: 50%
//              duration: 500ms
//            - state: False
//              duration: 250ms



//COMB				One after anohter
//IN WAVES
//SEQUENTIAL		Done
//SLO-GLO
//CHASING/FLASH
//SLOW FADE
//TWINKLE/FLASH
//STEADY ON			None
