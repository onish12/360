#include <windows.h>
#include <setupapi.h>
#include <newdev.h>
#include <cstddef>
static_assert(sizeof(void*) == 8);
static_assert(sizeof(SP_DEVINFO_DATA) == 32);
static_assert(sizeof(SP_DEVINSTALL_PARAMS_W) == 584);
static_assert(offsetof(SP_DEVINSTALL_PARAMS_W, DriverPath) == 60);
static_assert(sizeof(SP_DRVINFO_DATA_V2_W) == 1568);
static_assert(offsetof(SP_DRVINFO_DATA_V2_W, DriverVersion) == 1560);
static_assert(DIIDFLAG_INSTALLNULLDRIVER == 4);
static_assert(DI_ENUMSINGLEINF == 0x10000);
static_assert(SPDIT_COMPATDRIVER == 2);
static_assert(SPDRP_HARDWAREID == 1);
int main() { return 0; }
