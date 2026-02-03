#pragma once

#include <atomic>

void test_gpio ();

void gpio_innit();

void gpio_exit();


void gpio_t1();

void gpio_t2();

void trigger_thread(std::atomic<int>& atom);
