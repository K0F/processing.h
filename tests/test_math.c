/* Unit tests for pure processing.h functions — min()/max() math and the
 * real delay() environment call. Headless: no window is opened, and the only
 * GPU-touching code that needs linking is satisfied by the static archives. */

#include "processing.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int failures = 0;
static int checks   = 0;

#define CHECK(cond, ...) do { \
  checks++; \
  if (!(cond)) { \
    failures++; \
    fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); \
    fprintf(stderr, "\n"); \
  } \
} while (0)

static void test_min2_scalar(void) {
  CHECK(min(3, 7) == 3, "min(3,7)=%d want 3", min(3, 7));
  CHECK(min(7, 3) == 3, "min(7,3)=%d want 3", min(7, 3));
  CHECK(min(5, 5) == 5, "min(5,5)=%d want 5", min(5, 5));
  CHECK(min(2.5f, 1.5f) == 1.5f, "min(2.5,1.5)=%f want 1.5", min(2.5f, 1.5f));
  CHECK(min(-4, 2) == -4, "min(-4,2)=%d want -4", min(-4, 2));
}

static void test_max2_scalar(void) {
  CHECK(max(3, 7) == 7, "max(3,7)=%d want 7", max(3, 7));
  CHECK(max(7, 3) == 7, "max(7,3)=%d want 7", max(7, 3));
  CHECK(max(5, 5) == 5, "max(5,5)=%d want 5", max(5, 5));
  CHECK(max(2.5f, 1.5f) == 2.5f, "max(2.5,1.5)=%f want 2.5", max(2.5f, 1.5f));
  CHECK(max(-4, 2) == 2, "max(-4,2)=%d want 2", max(-4, 2));
}

static void test_min3_scalar(void) {
  CHECK(min(3, 7, 1) == 1, "min(3,7,1)=%d want 1", min(3, 7, 1));
  CHECK(min(1, 7, 3) == 1, "min(1,7,3)=%d want 1", min(1, 7, 3));
  CHECK(min(9, 4, 6) == 4, "min(9,4,6)=%d want 4", min(9, 4, 6));
  CHECK(min(2.5f, 1.5f, 8.0f) == 1.5f, "min(2.5,1.5,8)=%f want 1.5", min(2.5f, 1.5f, 8.0f));
}

static void test_max3_scalar(void) {
  CHECK(max(3, 7, 1) == 7, "max(3,7,1)=%d want 7", max(3, 7, 1));
  CHECK(max(1, 7, 3) == 7, "max(1,7,3)=%d want 7", max(1, 7, 3));
  CHECK(max(9, 4, 6) == 9, "max(9,4,6)=%d want 9", max(9, 4, 6));
  CHECK(max(2.5f, 1.5f, 8.0f) == 8.0f, "max(2.5,1.5,8)=%f want 8.0", max(2.5f, 1.5f, 8.0f));
}

static void test_min_max_int_array(void) {
  int a[5] = { 5, -3, 9, 0, 2 };
  CHECK(min(a) == -3, "min(int[5])=%d want -3", min(a));
  CHECK(max(a) == 9, "max(int[5])=%d want 9", max(a));
  int b[1] = { 42 };
  CHECK(min(b) == 42, "min(int[1])=%d want 42", min(b));
  CHECK(max(b) == 42, "max(int[1])=%d want 42", max(b));
}

static void test_min_max_float_array(void) {
  float a[4] = { 2.5f, -1.5f, 8.0f, 0.25f };
  CHECK(min(a) == -1.5f, "min(float[4])=%f want -1.5", min(a));
  CHECK(max(a) == 8.0f, "max(float[4])=%f want 8.0", max(a));
}

static void test_min_max_heap_array(void) {
  int *a = calloc(4, sizeof(int));
  a[0] = 10; a[1] = -20; a[2] = 30; a[3] = 5;
  _pde_arr_register(a, 4, sizeof(int));
  CHECK(min(a) == -20, "min(heap int[4])=%d want -20", min(a));
  CHECK(max(a) == 30, "max(heap int[4])=%d want 30", max(a));
  free(a);

  float *f = calloc(3, sizeof(float));
  f[0] = 1.5f; f[1] = 9.25f; f[2] = -0.5f;
  _pde_arr_register(f, 3, sizeof(float));
  CHECK(min(f) == -0.5f, "min(heap float[3])=%f want -0.5", min(f));
  CHECK(max(f) == 9.25f, "max(heap float[3])=%f want 9.25", max(f));
  free(f);
}

static void test_min_arr_count_form(void) {
  int a[4] = { 1, 2, 3, 4 };
  CHECK(_pde_min_arr_int(a, 3) == 1, "explicit-count min=%d want 1", _pde_min_arr_int(a, 3));
  CHECK(_pde_min_arr_int(a, 0) == 0, "empty min=%d want 0", _pde_min_arr_int(a, 0));
  float f[3] = { 8.0f, 3.0f, 5.0f };
  CHECK(_pde_max_arr_float(f, 2) == 8.0f, "explicit-count max=%f want 8.0", _pde_max_arr_float(f, 2));
}

static void test_delay(void) {
  struct timespec t0, t1;
  clock_gettime(CLOCK_MONOTONIC, &t0);
  delay(200);
  clock_gettime(CLOCK_MONOTONIC, &t1);
  double elapsed = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
  CHECK(elapsed >= 0.15, "delay(200) elapsed %.3fs want >=0.15", elapsed);
  CHECK(elapsed < 2.0, "delay(200) elapsed %.3fs want <2", elapsed);
  delay(0);
  delay(-5);
}

static void test_binary_unbinary(void) {
  CHECK(strcmp(binary(205), "00000000000000000000000011001101") == 0,
        "binary(205)=%s", binary(205));
  CHECK(strlen(binary(5)) == 32, "binary(5) len=%zu want 32", strlen(binary(5)));
  CHECK(strcmp(binary(5) + 29, "101") == 0, "binary(5) suffix=%s", binary(5) + 29);
  CHECK(strcmp(binary(0), "00000000000000000000000000000000") == 0, "binary(0)=%s", binary(0));
  CHECK(strcmp(binary(5, 8), "00000101") == 0, "binary(5,8)=%s", binary(5, 8));
  CHECK(strcmp(binary(0xFFFFCC00u), "11111111111111111100110000000000") == 0,
        "binary(0xFFFFCC00)=%s", binary(0xFFFFCC00u));
  CHECK(strcmp(binary(0xFFFFCC00u, 16), "1100110000000000") == 0,
        "binary(0xFFFFCC00,16)=%s", binary(0xFFFFCC00u, 16));
  CHECK(strcmp(binary(5, 32), binary(5)) == 0, "binary(5,32) != binary(5)");
  CHECK(strcmp(binary(5, 40), binary(5)) == 0, "binary(5,40) should clamp to 32");
  CHECK(strcmp(binary(5, 0), "") == 0, "binary(5,0)=%s want empty", binary(5, 0));
  CHECK(unbinary("00001000") == 8, "unbinary(00001000)=%d want 8", unbinary("00001000"));
  CHECK(unbinary("11001101") == 205, "unbinary(11001101)=%d want 205", unbinary("11001101"));
  CHECK(unbinary(binary(205)) == 205, "unbinary(binary(205))=%d want 205", unbinary(binary(205)));
  CHECK(unbinary(binary(-1)) == -1, "unbinary(binary(-1))=%d want -1", unbinary(binary(-1)));
  CHECK(unbinary("11111111111111111111111111111111") == -1,
        "unbinary(all ones)=%d want -1", unbinary("11111111111111111111111111111111"));
  CHECK(unbinary("-101") == -5, "unbinary(-101)=%d want -5", unbinary("-101"));
  CHECK(unbinary("") == 0, "unbinary(\"\")=%d want 0", unbinary(""));
}

int main(void) {
  test_min2_scalar();
  test_max2_scalar();
  test_min3_scalar();
  test_max3_scalar();
  test_min_max_int_array();
  test_min_max_float_array();
  test_min_max_heap_array();
  test_min_arr_count_form();
  test_delay();
  test_binary_unbinary();
  printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
