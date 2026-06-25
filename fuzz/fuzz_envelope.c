#include <stddef.h>
#include <stdint.h>

#include "hermes/envelope.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    hermes_envelope env;
    if (hermes_envelope_decode(data, size, &env) == HERMES_OK) {
        (void) hermes_envelope_verify(&env, 0u);
    }
    return 0;
}
