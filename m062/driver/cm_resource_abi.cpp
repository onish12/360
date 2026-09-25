// SPDX-License-Identifier: MIT
#include <ntddk.h>

// M0.6.15F3: compile-time ABI contract for the user-mode SPDRP_ALLOC_CONFIG
// parser. These assertions are checked by the real WDK build, not the fake WDF
// host shim. Windows defines CM_PARTIAL_RESOURCE_DESCRIPTOR under pack(4).
static_assert(sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR)==20,
              "CM_PARTIAL_RESOURCE_DESCRIPTOR must be 20 bytes under WDK pack(4)");
static_assert(FIELD_OFFSET(CM_PARTIAL_RESOURCE_DESCRIPTOR,u)==4,
              "CM partial descriptor union must start at byte 4");
static_assert(FIELD_OFFSET(CM_RESOURCE_LIST,List)==4,
              "CM_RESOURCE_LIST first full descriptor must start at byte 4");
static_assert(FIELD_OFFSET(CM_FULL_RESOURCE_DESCRIPTOR,PartialResourceList)==8,
              "CM_FULL_RESOURCE_DESCRIPTOR partial list must start at byte 8");
static_assert(FIELD_OFFSET(CM_PARTIAL_RESOURCE_LIST,PartialDescriptors)==8,
              "CM_PARTIAL_RESOURCE_LIST descriptor array must start at byte 8");
static_assert(FIELD_OFFSET(CM_FULL_RESOURCE_DESCRIPTOR,PartialResourceList)+
              FIELD_OFFSET(CM_PARTIAL_RESOURCE_LIST,PartialDescriptors)==16,
              "CM full descriptor header must be 16 bytes");
static_assert(CM_RESOURCE_INTERRUPT_MESSAGE==0x0002,
              "message interrupt discriminator changed unexpectedly");
