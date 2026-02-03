#include <iostream>
#include <unistd.h>

#include <jetgpio.h>

#include "gpio.H"
#include "time.H"

#define GPIO_SAVECALL(call) \
    { \
        int res = call; \
        if (res < 0) { \
            printf("Jetgpio Error after %s (%d)\n", #call, res); \
            std::abort(); \
        } \
    }

void test_gpio () {

    std::cout << "testing GPIO" << std::endl;

    GPIO_SAVECALL(gpioInitialise());


    //GPIO_SAVECALL(gpioSetPWMfrequency(13, 50)); // 10 Hz
    //GPIO_SAVECALL(gpioPWM(32, 26)); // 10 % duty cycle


    GPIO_SAVECALL(gpioSetMode(38, JET_OUTPUT));


    for (int i = 0; i<10000; ++i) {

        std::cout << "GPIO PIN 38 write: 1" << std::endl;

        GPIO_SAVECALL(gpioWrite(38, 1));

        sleep(1);

        std::cout << "GPIO PIN 38 write: 0" << std::endl;

        GPIO_SAVECALL(gpioWrite(38, 0));

        sleep(1);

    }

    gpioTerminate();
}


void gpio_innit() {
    GPIO_SAVECALL(gpioInitialise());

    GPIO_SAVECALL(gpioSetMode(35, JET_OUTPUT));
    GPIO_SAVECALL(gpioSetMode(38, JET_OUTPUT));
}

void gpio_exit() {
    gpioTerminate();
}


void gpio_t1() {
    GPIO_SAVECALL(gpioWrite(38, 1));
    sleep_us(50);
    GPIO_SAVECALL(gpioWrite(38, 0));
}

void gpio_t2() {
    GPIO_SAVECALL(gpioWrite(35, 1));
    sleep_us(50);
    GPIO_SAVECALL(gpioWrite(35, 0));
}


void trigger_thread(std::atomic<int>& atom) {

    auto last = get_time();
    while (atom.load() == 0) {

        while (time_diff_us(last, get_time()) < 1000.) {}
        last = get_time();

        gpio_t1();

    }
}

