#pragma once

#include <atomic>

void test_gpio ();
void test_gpio2 ();

void gpio_innit();

void gpio_exit();


void gpio_t1();

void gpio_t2();

void trigger_thread(std::atomic<int>& atom);



void test_function(double * data, long long size);
