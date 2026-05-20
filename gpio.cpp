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


void test_gpio2 () {

    std::cout << "testing GPIO" << std::endl;

    GPIO_SAVECALL(gpioInitialise());
    GPIO_SAVECALL(gpioSetMode(38, JET_OUTPUT));

    int handle = i2cOpen(0, 2);

    GPIO_SAVECALL(handle);

    unsigned i2cAddr = 0x58;
    unsigned regVout0 = 0x02; // VOUT0

    std::chrono::microseconds timeout(10);
    std::future<int> next_val = std::async(get_async);

    // GPIO_SAVECALL(i2cWriteByteData(handle, i2cAddr, 0, 0 ));
    GPIO_SAVECALL(i2cWriteByteData(handle, i2cAddr, 1, 0x11 ));

    int val = 0;
    int last_val = 0;

    while (true) {

        GPIO_SAVECALL(gpioWrite(38, 1));

        GPIO_SAVECALL(i2cWriteWordData(handle, i2cAddr, regVout0, static_cast<unsigned>(val)));

        // for (int i = 0; i < 500; ++i) {

        //     //dutycycle = static_cast<int>(256 * (std::sin(i / 10.)*0.5 + 0.5));

        //     val = static_cast<int>(
        //         65536 * batman(i * (13.98/500.) - 6.99)
        //     );

        //     // GPIO_SAVECALL(gpioPWM(18, dutycycle));
        //     GPIO_SAVECALL(i2cWriteWordData(handle, i2cAddr, regVout0, static_cast<unsigned>(val)));

        //     // sleep_us(30);
        // }


        sleep_us(500);

        GPIO_SAVECALL(gpioWrite(38, 0));

        GPIO_SAVECALL(i2cWriteWordData(handle, i2cAddr, regVout0, static_cast<unsigned>(last_val)));

        sleep_us(500);

        if (next_val.wait_for(timeout) == std::future_status::ready) {
            last_val = val;
            val = next_val.get();
            if (val < 0) {
                break;
            }
            next_val = std::async(get_async);
        }
    }


    GPIO_SAVECALL(i2cWriteWordData(handle, i2cAddr, regVout0, 0 ));

    GPIO_SAVECALL(i2cClose(handle));

    gpioTerminate();
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

void test_function(double * data, long long size) {

    for (long long i=0; i<size; ++i) {
        data[i] += static_cast<double>(i);
    }
}

/*


import time
from smbus import SMBus

# Jetson I2C bus usually defaults to 1
I2C_BUS = 1
DEVICE_ADDR = 0x58  # Default I2C address for GP8XXX modules

# GP8XXX Configuration Registers
GP8XXX_CONFIG_CURRENT_REG = 0x02

def set_dac_voltage(bus, voltage, v_max=10.0, resolution=2**15):
    """
    Calculates the 12-bit DAC value and writes it to the GP8XXX over I2C.
    v_max is the configured output range of the module (e.g., 5.0V or 10.0V).
    """
    # Constrain voltage within limits
    if voltage < 0: voltage = 0
    if voltage > v_max: voltage = v_max
    # Calculate digital value
    dac_value = int((voltage / v_max) * resolution)
    # Split into 2 bytes (low byte, high byte)
    low_byte = dac_value & 0xFF
    high_byte = (dac_value >> 8) & 0xFF
    # Send data to channel 0
    bus.write_i2c_block_data(DEVICE_ADDR, GP8XXX_CONFIG_CURRENT_REG, [low_byte, high_byte])

try:
    bus = SMBus(I2C_BUS)
    print(I2C_BUS)
    set_dac_voltage(bus, 3.5, v_max=10.0)
    print("Voltage set to 3.5V")
    time.sleep(2)
    set_dac_voltage(bus, 5.0, v_max=10.0)
    print("Voltage set to 5.0V")
except Exception as e:
    print(e)
finally:
    bus.close()

*/