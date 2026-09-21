// SPDX-License-Identifier: MIT
#include "../m062/driver/telemetry.h"
#include <cstdio>
#include <cstdlib>

using namespace phaser360::windows;

static unsigned checks=0;
#define CHECK(x) do { ++checks; if(!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)

int main() {
    TelemetryState state;
    TelemetrySnapshotV1 s{};
    state.Snapshot(&s);
    CHECK(s.version==1 && s.size==sizeof(TelemetrySnapshotV1));
    CHECK(s.flags==0 && s.sessionGeneration==0 && s.completedD0==0 &&
          s.failedD0==0 && s.lastD0Status==STATUS_INVALID_DEVICE_STATE);

    state.SetFlag(TelemetryFirmwareLoaded,true);
    state.SetFlag(TelemetryResourcesPrepared,true);
    state.SetSessionGeneration(7);
    state.SetLastD0Status(STATUS_SUCCESS);
    state.IncrementCompleted();
    state.IncrementCompleted();
    state.IncrementFailed();
    state.Snapshot(&s);
    CHECK((s.flags & TelemetryFirmwareLoaded)!=0);
    CHECK((s.flags & TelemetryResourcesPrepared)!=0);
    CHECK((s.flags & TelemetryD0Active)==0);
    CHECK((s.flags & TelemetryRemoved)==0);
    CHECK(s.sessionGeneration==7 && s.completedD0==2 &&
          s.failedD0==1 && s.lastD0Status==STATUS_SUCCESS);

    state.SetFlag(TelemetryD0Active,true);
    state.SetFlag(TelemetryRemoved,true);
    state.SetFlag(TelemetryResourcesPrepared,false);
    state.SetLastD0Status(STATUS_DEVICE_CONFIGURATION_ERROR);
    state.Snapshot(&s);
    CHECK((s.flags & TelemetryFirmwareLoaded)!=0);
    CHECK((s.flags & TelemetryResourcesPrepared)==0);
    CHECK((s.flags & TelemetryD0Active)!=0);
    CHECK((s.flags & TelemetryRemoved)!=0);
    CHECK(s.lastD0Status==STATUS_DEVICE_CONFIGURATION_ERROR);

    state.SetFlag(TelemetryD0Active,false);
    state.Snapshot(&s);
    CHECK((s.flags & TelemetryD0Active)==0);
    CHECK(s.reserved==0);

    std::printf("H11_TELEMETRY_STATE_TESTS=%u PASS; atomic_mirror=YES; hardware=NONE\n",checks);
}
