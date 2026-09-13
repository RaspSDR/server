#include "si5351/I2CInterface.h"
#include "si5351/si5351.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>

struct write_t {
    uint8_t reg;
    uint8_t value;
};

class FakeI2C : public I2CInterface {
public:
    FakeI2C() : fail_write(false), fail_read_on(0), read_count(0) {
        memset(regs, 0, sizeof(regs));
        regs[SI5351_OUTPUT_ENABLE_CTRL] = 0xff;
    }

    uint8_t check_address(uint8_t) {
        return 0;
    }

    bool read(uint8_t, uint8_t reg, uint8_t* data) {
        read_count++;
        if (fail_read_on != 0 && read_count == fail_read_on) {
            return false;
        }
        *data = regs[reg];
        return true;
    }

    uint8_t write(uint8_t, uint8_t reg, uint8_t data) {
        if (fail_write) {
            fail_write = false;
            return 4;
        }
        regs[reg] = data;
        writes.push_back({ reg, data });
        return 0;
    }

    uint8_t write_bulk(uint8_t, uint8_t reg, uint8_t count, uint8_t* data) {
        for (uint8_t i = 0; i < count; i++) {
            uint8_t rc = write(0, reg + i, data[i]);
            if (rc != 0) return rc;
        }
        return 0;
    }

    void clear_writes() {
        writes.clear();
    }

    void reset_reads() {
        read_count = 0;
        fail_read_on = 0;
    }

    uint8_t regs[256];
    std::vector<write_t> writes;
    bool fail_write;
    int fail_read_on;
    int read_count;
};

static int write_index(const std::vector<write_t>& writes, uint8_t reg,
    bool output_disabled) {
    for (unsigned i = 0; i < writes.size(); i++) {
        if (writes[i].reg != reg) continue;
        if (reg != SI5351_OUTPUT_ENABLE_CTRL ||
            ((writes[i].value & 1) != 0) == output_disabled)
            return i;
    }
    return -1;
}

static uint32_t decode_p1(const uint8_t* regs, uint8_t base) {
    return ((uint32_t)(regs[base + 2] & 0x03) << 16) |
        ((uint32_t) regs[base + 3] << 8) |
        regs[base + 4];
}

static uint32_t decode_p2(const uint8_t* regs, uint8_t base) {
    return ((uint32_t)(regs[base + 5] & 0x0f) << 16) |
        ((uint32_t) regs[base + 6] << 8) |
        regs[base + 7];
}

static void test_profile(uint32_t adc_hz, uint32_t pll_hz,
    uint8_t expected_pll_mult, uint8_t expected_ms_div) {
    FakeI2C bus;
    Si5351 clock(0x60, &bus);
    assert(clock.init(SI5351_CRYSTAL_LOAD_0PF, 24576000, 0));

    clock.clear_io_error();
    bus.clear_writes();
    clock.set_ms_source(SI5351_CLK0, SI5351_PLLB);
    assert(clock.set_freq_manual(
        (uint64_t) adc_hz * 100, (uint64_t) pll_hz * 100, SI5351_CLK0) == 0);
    assert(!clock.io_error_detected());
    assert(clock.pll_assignment[SI5351_CLK0] == SI5351_PLLB);
    assert((bus.regs[SI5351_CLK0_CTRL] & SI5351_CLK_PLL_SELECT) != 0);
    assert((bus.regs[SI5351_CLK0_CTRL] & SI5351_CLK_INTEGER_MODE) != 0);

    uint32_t pll_p1 = decode_p1(bus.regs, SI5351_PLLB_PARAMETERS);
    uint32_t ms_p1 = decode_p1(bus.regs, SI5351_CLK0_PARAMETERS);
    assert(pll_p1 == 128U * expected_pll_mult - 512U);
    assert(decode_p2(bus.regs, SI5351_PLLB_PARAMETERS) == 0);
    assert(ms_p1 == 128U * expected_ms_div - 512U);
    assert(decode_p2(bus.regs, SI5351_CLK0_PARAMETERS) == 0);

    int disable_i = write_index(bus.writes, SI5351_OUTPUT_ENABLE_CTRL, true);
    int ms_i = write_index(bus.writes, SI5351_CLK0_PARAMETERS, false);
    int pll_i = write_index(bus.writes, SI5351_PLLB_PARAMETERS, false);
    int reset_i = write_index(bus.writes, SI5351_PLL_RESET, false);
    int enable_i = write_index(bus.writes, SI5351_OUTPUT_ENABLE_CTRL, false);
    assert(disable_i >= 0);
    assert(ms_i > disable_i && pll_i > disable_i);
    assert(ms_i < reset_i && pll_i < reset_i);
    assert(enable_i > reset_i);

    clock.clear_io_error();
    bus.clear_writes();
    bus.fail_write = true;
    assert(clock.set_freq_manual(
        (uint64_t) adc_hz * 100, (uint64_t) pll_hz * 100, SI5351_CLK0) != 0);
    assert(clock.io_error_detected());
    assert(bus.writes.empty());

    clock.clear_io_error();
    bus.clear_writes();
    bus.reset_reads();
    bus.fail_read_on = 1;
    assert(clock.set_freq_manual(
        (uint64_t) adc_hz * 100, (uint64_t) pll_hz * 100, SI5351_CLK0) != 0);
    assert(clock.io_error_detected());
    assert(bus.writes.empty());

    clock.clear_io_error();
    bus.clear_writes();
    bus.reset_reads();
    bus.fail_read_on = 2;
    assert(clock.set_freq_manual(
        (uint64_t) adc_hz * 100, (uint64_t) pll_hz * 100, SI5351_CLK0) != 0);
    assert(clock.io_error_detected());
    assert(bus.writes.size() == 1);
    assert(bus.writes[0].reg == SI5351_OUTPUT_ENABLE_CTRL);
    assert((bus.writes[0].value & 1) != 0);
}

static void test_init_failures() {
    FakeI2C bus;
    bus.regs[SI5351_DEVICE_STATUS] = SI5351_STATUS_SYS_INIT;
    Si5351 stuck_clock(0x60, &bus);
    assert(!stuck_clock.init(SI5351_CRYSTAL_LOAD_0PF, 24576000, 0));

    FakeI2C read_error_bus;
    read_error_bus.fail_read_on = 1;
    Si5351 read_error_clock(0x60, &read_error_bus);
    assert(!read_error_clock.init(SI5351_CRYSTAL_LOAD_0PF, 24576000, 0));
    assert(read_error_clock.io_error_detected());
}

int main() {
    test_init_failures();
    test_profile(98304000, 786432000, 32, 8);
    test_profile(110592000, 663552000, 27, 6);
    puts("airband Si5351 tests passed");
    return 0;
}
