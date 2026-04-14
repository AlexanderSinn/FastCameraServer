#include <iostream>
#include <future>
#include <cmath>
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



int get_async() {
    int res = 0;
    std::cin >> res;
    return res;
}

double batman(double x) {
    using std::abs;
    using std::sqrt;
    auto iff =  [](double a, double b, double c){ return a ? b : c; };

    return
    iff(x<-7, 0,
        iff(x<-3, 3*sqrt(1-x*x/49),
            iff(x<-1, (6*sqrt(10)/7 + (1.5-0.5*abs(x)) - (6*sqrt(10)/14)*sqrt(4-(abs(x)-1)*(abs(x)-1)) ),
                iff(x<-0.75, 9-8*abs(x),
                    iff(x<-0.5, 3*abs(x)+0.75,
                        iff(x<=0.5, 2.25,
                            iff(x<=0.75, 3*abs(x)+0.75,
                                iff(x<=1, 9-8*abs(x),
                                    iff(x<=3, (6*sqrt(10)/7 + (1.5-0.5*abs(x)) - (6*sqrt(10)/14)*sqrt(4-(abs(x)-1)*(abs(x)-1)) ),
                                        iff(x<=7, 3*sqrt(1-x*x/49),
                                            0
                                        )
                                    )
                                )
                            )
                        )
                    )
                )
            )
        )
    )/3;
}


void test_gpio () {

    std::cout << "testing GPIO" << std::endl;

    GPIO_SAVECALL(gpioInitialise());


    //GPIO_SAVECALL(gpioSetPWMfrequency(13, 50)); // 10 Hz
    //GPIO_SAVECALL(gpioPWM(32, 26)); // 10 % duty cycle


    GPIO_SAVECALL(gpioSetMode(38, JET_OUTPUT));

    GPIO_SAVECALL(gpioSetPWMfrequency(18, 1593000));

    int dutycycle = 128;
    int old_dutycycle = 128;
    //int subcycle = 0;

    std::chrono::microseconds timeout(10);
    std::future<int> next_dutycycle = std::async(get_async);

    //double time = 0;

    for (;;) {

        GPIO_SAVECALL(gpioWrite(38, 1));

        GPIO_SAVECALL(gpioPWM(18, dutycycle));

        //GPIO_SAVECALL(gpioWrite(38, 0));

        sleep_us(500);

        /*

        for (int i = 0; i < 500; ++i) {

            //dutycycle = static_cast<int>(256 * (std::sin(i / 10.)*0.5 + 0.5));

            dutycycle = static_cast<int>(
                256 * batman(i * (13.98/500.) - 6.99)
            );

            GPIO_SAVECALL(gpioPWM(18, dutycycle));

            sleep_us(30);
        }

        */


        GPIO_SAVECALL(gpioWrite(38, 0));

        GPIO_SAVECALL(gpioPWM(18, old_dutycycle));
        //GPIO_SAVECALL(gpioPWM(18, 0));

        sleep_us(500);

        if (next_dutycycle.wait_for(timeout) == std::future_status::ready) {
            old_dutycycle = dutycycle;
            dutycycle = next_dutycycle.get();
            if (dutycycle < 0) {
                break;
            }
            next_dutycycle = std::async(get_async);
        }

        //time += 1.;

        //++subcycle;
        //if (subcycle == 50) {
        //    subcycle = 0;
        //    dutycycle = (dutycycle + 1) % 257;
        //}

        //dutycycle = static_cast<int>(256 * (std::sin(time / 200)*0.5 + 0.5));
    }

    GPIO_SAVECALL(gpioWrite(38, 0));


    GPIO_SAVECALL(gpioPWM(18, 0));


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

