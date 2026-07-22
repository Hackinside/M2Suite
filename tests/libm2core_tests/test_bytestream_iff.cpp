// Minimal assert-based test runner (no external test framework dependency
// yet) — each check aborts with a message on failure, exit code 0 means
// pass. Run via CTest.
#include <cstdio>
#include <cstdlib>

#include "m2core/ByteStream.h"
#include "m2core/Error.h"
#include "m2core/Iff.h"

#define CHECK(cond)                                                             \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::fprintf(stderr, "CHECK FAILED: %s (%s:%d)\n", #cond, __FILE__, \
                          __LINE__);                                            \
            std::exit(1);                                                       \
        }                                                                       \
    } while (0)

using namespace m2core;

static void testByteStreamRoundTrip() {
    ByteWriter w;
    w.writeU8(0x12);
    w.writeU16BE(0xABCD);
    w.writeU32BE(0xDEADBEEF);
    w.writeF32BE(3.5f);
    w.writeBytes("hello", 5);

    ByteReader r(w.bytes());
    CHECK(r.readU8() == 0x12);
    CHECK(r.readU16BE() == 0xABCD);
    CHECK(r.readU32BE() == 0xDEADBEEF);
    CHECK(r.readF32BE() == 3.5f);
    auto bytes = r.readBytes(5);
    CHECK(bytes.size() == 5 && bytes[0] == 'h' && bytes[4] == 'o');
    CHECK(r.atEnd());
}

static void testByteReaderBoundsChecking() {
    uint8_t data[2] = {1, 2};
    ByteReader r(data, 2);
    bool threw = false;
    try {
        r.readU32BE();
    } catch (const FormatError&) {
        threw = true;
    }
    CHECK(threw);
}

static void testIffFormRoundTrip() {
    IffFormWriter w(makeId('T', 'E', 'S', 'T'));
    std::vector<uint8_t> a = {1, 2, 3};        // 3 bytes -> needs 1 pad byte to reach 4-alignment
    std::vector<uint8_t> b = {4, 5, 6, 7, 8};  // 5 bytes -> needs 3 pad bytes
    w.addChunk(makeId('A', 'A', 'A', 'A'), a);
    w.addChunk(makeId('B', 'B', 'B', 'B'), b);
    std::vector<uint8_t> bytes = w.finish();

    ByteReader r(bytes);
    IffForm form = IffForm::parse(r);
    CHECK(form.formType() == makeId('T', 'E', 'S', 'T'));
    CHECK(form.has(makeId('A', 'A', 'A', 'A')));
    CHECK(form.has(makeId('B', 'B', 'B', 'B')));
    CHECK(!form.has(makeId('C', 'C', 'C', 'C')));

    const auto* aOut = form.find(makeId('A', 'A', 'A', 'A'));
    CHECK(aOut && *aOut == a);
    const auto* bOut = form.find(makeId('B', 'B', 'B', 'B'));
    CHECK(bOut && *bOut == b);
}

static void testIffFormRejectsBadMagic() {
    ByteWriter w;
    w.writeU32BE(makeId('N', 'O', 'P', 'E'));
    w.writeU32BE(4);
    w.writeU32BE(makeId('X', 'X', 'X', 'X'));
    ByteReader r(w.bytes());
    bool threw = false;
    try {
        IffForm::parse(r);
    } catch (const FormatError&) {
        threw = true;
    }
    CHECK(threw);
}

int main() {
    testByteStreamRoundTrip();
    testByteReaderBoundsChecking();
    testIffFormRoundTrip();
    testIffFormRejectsBadMagic();
    std::printf("libm2core_tests: all checks passed\n");
    return 0;
}
