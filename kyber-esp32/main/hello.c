#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>

#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_random.h"      // HW TRNG

#include "params.h"
#include "poly.h"
#include "polyvec.h"
#include "indcpa.h"          // gen_matrix()
#include "fips202.h"         // SHAKE256

// =================== Config ===================
#ifndef TRIGGER_GPIO
#define TRIGGER_GPIO 2
#endif

#ifndef KYBER_TRANSPOSE
#define KYBER_TRANSPOSE 0     // 0 = A, 1 = A^T (por si quieres experimentar)
#endif

#ifndef USE_FIXED_SEED
#define USE_FIXED_SEED 0      // 1 para reproducibilidad exacta
#endif

#if USE_FIXED_SEED
// static seed
static const uint8_t FIXED_SEED_RAW[32] = {
    0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
    0x88,0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,
    0x10,0x20,0x30,0x40,0x50,0x60,0x70,0x80,
    0x90,0xA0,0xB0,0xC0,0xD0,0xE0,0xF0,0x01
};
#endif


// ---------- trigger GPIO power/side  ----------
static inline void trigger_init(void){
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << TRIGGER_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    gpio_set_level(TRIGGER_GPIO, 0);
}
static inline void trigger_high(void){ gpio_set_level(TRIGGER_GPIO, 1); }
static inline void trigger_low(void){  gpio_set_level(TRIGGER_GPIO, 0); }



static void print_hex_raw(const uint8_t *buf, size_t len){
    for (size_t i = 0; i < len; i++) printf("%02X", buf[i]);
    printf("\n");
}


static void dump_polyvec_raw(const polyvec *A){
    // Cabecera machine-parseable
    printf("MAT:A:K=%d,N=%d,WORD=16,ENDIAN=LE\n", KYBER_K, KYBER_N);
    for (int i = 0; i < KYBER_K; i++) {
        printf("A:%d:", i);
        for (int j = 0; j < KYBER_N; j++) {
            uint16_t v = (uint16_t)A->vec[i].coeffs[j];
            // little-endian
            uint8_t lo = (uint8_t)(v & 0xFF);
            uint8_t hi = (uint8_t)(v >> 8);
            printf("%02X%02X", lo, hi);
        }
        printf("\n");
    }
}

void app_main(void){
    trigger_init();

    // Buffers
    uint8_t seed_raw[32];   // trng
    uint8_t rho[32];        // seed expand with SHAKE256 
    static polyvec A[KYBER_K];

    // ====== trng ======
#if USE_FIXED_SEED
    memcpy(seed_raw, FIXED_SEED_RAW, sizeof(seed_raw));
    int64_t t_trng_us = 0; 
#else
    int64_t t0 = esp_timer_get_time();
    trigger_high();
    for (int i = 0; i < 32; i += 4) {
        uint32_t r = esp_random();
        memcpy(seed_raw + i, &r, 4);
    }
    trigger_low();
    int64_t t1 = esp_timer_get_time();
    int64_t t_trng_us = t1 - t0;
#endif

    int64_t t2 = esp_timer_get_time();
    trigger_high();
    {
        keccak_state st;
        shake256_init(&st);
        shake256_absorb(&st, seed_raw, sizeof(seed_raw));
        shake256_finalize(&st);
        shake256_squeeze(rho, sizeof(rho), &st);
    }
    trigger_low();
    int64_t t3 = esp_timer_get_time();
    int64_t t_shake_us = t3 - t2;

    // generation of A
    int64_t t4 = esp_timer_get_time();
    trigger_high();
    gen_matrix(A, rho, KYBER_TRANSPOSE);
    trigger_low();
    int64_t t5 = esp_timer_get_time();
    int64_t t_genA_us = t5 - t4;


    printf("BEGIN\n");
#if USE_FIXED_SEED
    printf("MODE:FIXED\n");
#else
    printf("MODE:TRNG\n");
#endif
    printf("RAW:SEED_RAW:");
    print_hex_raw(seed_raw, sizeof(seed_raw));

    printf("RAW:RHO:");
    print_hex_raw(rho, sizeof(rho));

    printf("TIME:TRNG_US:%" PRId64 "\n", t_trng_us);
    printf("TIME:SHAKE_US:%" PRId64 "\n", t_shake_us);
    printf("TIME:GENMATRIX_US:%" PRId64 "\n", t_genA_us);

    dump_polyvec_raw(A);

    printf("END\n");

    
    while (1) { /* opctional: vTaskDelay(pdMS_TO_TICKS(1000)); */ }
}
