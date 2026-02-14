#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct address_space {
    uint32_t* page_directory;   // fizic + virtual mapped
} address_space_t;

address_space_t* address_space_create();
void address_space_destroy(address_space_t* as);
void address_space_switch(address_space_t* as);

#ifdef __cplusplus
}
#endif
