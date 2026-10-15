#include "CommandHandler.h"
#include "BleFrames.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

static int rejectedCases = 0;

static bool sameConfig(const ControlConfiguration& a, const ControlConfiguration& b) {
    if (a.presetId != b.presetId) return false;
    for (int i=0; i<PWM_CH_COUNT; ++i) {
        const auto& p=a.channels[i]; const auto& q=b.channels[i];
        if (p.frequency!=q.frequency || p.duty!=q.duty || p.dutyMin!=q.dutyMin ||
            p.waveform!=q.waveform || p.phase!=q.phase || p.enabled!=q.enabled ||
            a.burstFrequency[i]!=b.burstFrequency[i]) return false;
    }
    return true;
}

struct Fixture {
    MagnetoController ctrl;
    BatteryMonitor battery;
    BLEManager ble;
    StorageManager storage;
    CommandHandler handler;
    Fixture() : handler(ctrl,battery,ble,storage) {
        testClockUs=0; hardwareWrites=0; outputDuty=0;
        rmtEnabled=false; macroTimerRunning=false;
        ctrl.begin();
    }
    void send(const std::string& json) { handler.handle(String(json)); }
    void ok(const char* kind) {
        JsonDocument response;
        assert(!ble.notifications.empty());
        assert(!deserializeJson(response,ble.notifications.back()));
        if (!response["ok"].is<const char*>()) {
            std::cerr << "Expected ok=" << kind << ", got " << ble.notifications.back() << "\n";
            std::abort();
        }
        assert(std::string(response["ok"].as<const char*>())==kind);
    }
    void start() { send(R"({"cmd":"start","duration":1,"freq":50,"intensity":20,"wave":4})"); ok("started"); }
    void pause() { send(R"({"cmd":"pause"})"); ok("paused"); }
};

static void rejected(Fixture& f, const std::string& command) {
    const auto config=f.ctrl.getConfiguration();
    const auto state=f.ctrl.getState();
    const auto time=f.ctrl.getTreatmentStatus();
    const int writes=f.storage.writes, peripheralWrites=hardwareWrites;
    const auto notifications=f.ble.notifications.size();
    f.send(command);
    JsonDocument response;
    if (f.ble.notifications.size()!=notifications+1 ||
        deserializeJson(response,f.ble.notifications.back()) ||
        !response["error"].is<const char*>() || !response["ok"].isNull()) {
        std::cerr << "Expected rejection: " << command << "\n";
        std::abort();
    }
    assert(sameConfig(config,f.ctrl.getConfiguration()));
    assert(state==f.ctrl.getState());
    const auto after=f.ctrl.getTreatmentStatus();
    assert(time.isActive==after.isActive && time.startTime==after.startTime);
    assert(time.totalDurationMs==after.totalDurationMs && time.remainingMs==after.remainingMs);
    assert(time.elapsedBeforePauseMs==after.elapsedBeforePauseMs);
    assert(writes==f.storage.writes && peripheralWrites==hardwareWrites);
    ++rejectedCases;
}

int main() {
    {
        BleFrames::Receiver rx;
        uint8_t a[]={0xA5,1,9,0,0,1,'a','b'};
        uint8_t b[]={0xA5,1,9,0,1,2,'c','d'};
        assert(rx.accept(a,sizeof(a),1)==BleFrames::Result::Incomplete);
        assert(rx.accept(b,sizeof(b),2)==BleFrames::Result::Complete);
        assert(rx.id==9 && rx.size==4 && memcmp(rx.data,"abcd",4)==0);
        rx.reset(); assert(rx.accept(b,sizeof(b),3)==BleFrames::Result::Invalid);
        rx.accept(a,sizeof(a),10);
        assert(rx.accept(b,sizeof(b),3011)==BleFrames::Result::Invalid);
        rx.accept(a,sizeof(a),4000); b[2]=8;
        assert(rx.accept(b,sizeof(b),4001)==BleFrames::Result::Invalid);
        std::cout << "Framing: complete, missing, expired and mismatched fragments PASS.\n";
    }

    // Cada dato inválido se combina con un cambio válido: antes se aplicaba parte.
    const std::vector<std::string> badFields={
        R"("freq":0)", R"("freq":9)", R"("intensity":9)", R"("intensity":50.1)", R"("wave":1)", R"("wave":2)", R"("freq":101)", R"("freq":-1)", R"("freq":50.5)",
        R"("freq":true)", R"("freq":"50")", R"("freq":4294967346)",
        R"("duty":-1)", R"("duty":65536)", R"("duty":4294967296)",
        R"("intensity":-1)", R"("intensity":60.1)", R"("intensity":true)",
        R"("intensity":"20")", R"("intensity":null)", R"("intensity":1e999)",
        R"("duty":10000,"intensity":-1)",
        R"("duty":10000,"intensity":20)",
        R"("wave":-1)", R"("wave":5)", R"("wave":256)",
        R"("wave":1,"intensity":2)", R"("wave":2,"intensity":0)",
        R"("wave":3,"phase":90)", R"("phase":-1)", R"("phase":361)",
        R"("ch":-2)", R"("ch":1)", R"("ch":2147483648)",
        R"("en":1)", R"("en":"false")", R"("burst_freq":-1)",
        R"("burst_freq":1)", R"("burst_freq":1000001)", R"("burst_freq":10000)",
        R"("unsupported":1)"
    };
    for (const char* cmd : {"start","set","resume"}) {
        for (const auto& field : badFields) {
            Fixture f;
            if (std::string(cmd)!="start") f.start();
            if (std::string(cmd)=="resume") f.pause();
            rejected(f,std::string("{\"cmd\":\"")+cmd+"\",\"phase\":0,"+field+"}");
        }
    }
    for (const char* duration : {"0","11","481","-1","1.5","true","null","\"5\"","4294967296"}) {
        Fixture f;
        rejected(f,std::string(R"({"cmd":"start","freq":75,"intensity":30,"duration":)")+duration+"}");
    }
    for (const char* id : {"-1","6","256","257","true","1.5","\"1\"","null"}) {
        Fixture f;
        rejected(f,std::string(R"({"cmd":"preset","id":)")+id+"}");
    }
    for (const char* json : {"[]","null","{}",R"({"cmd":1})",R"({"cmd":"unknown"})","{"}) {
        Fixture f;
        rejected(f,json);
    }
    {
        Fixture f;
        f.start();
        rejected(f,R"({"cmd":"start","freq":75,"duration":5})");
        rejected(f,R"({"cmd":"resume","freq":75})");
        rejected(f,R"({"cmd":"set","freq":75,"duration":5})");
        f.pause();
        rejected(f,R"({"cmd":"resume","freq":75,"duration":5})");
    }
    // Batería: no se guardan parámetros ni se inicia/reanuda una orden bloqueada.
    for (const char* cmd : {"start","resume","set","preset"}) {
        Fixture f;
        if (std::string(cmd)!="start") f.start();
        if (std::string(cmd)=="resume") f.pause();
        f.battery.empty=true;
        rejected(f,std::string(cmd)=="preset" ? R"({"cmd":"preset","id":1})" :
                 std::string("{\"cmd\":\"")+cmd+"\",\"freq\":75}");
        f.send(R"({"cmd":"stop"})"); f.ok("stopped"); // La parada sigue disponible.
    }
    // Temporización real del controlador: la pausa conserva el tiempo restante.
    {
        Fixture f; f.start();
        testClockUs=12000000; f.ctrl.update();
        assert(f.ctrl.getTreatmentStatus().remainingMs==48000);
        f.pause();
        testClockUs+=90000000; f.ctrl.update();
        assert(f.ctrl.getTreatmentStatus().remainingMs==48000 && outputDuty==0);
        rejected(f,R"({"cmd":"resume","freq":75,"wave":256})");
        f.send(R"({"cmd":"resume","freq":75,"intensity":30,"wave":4})"); f.ok("resumed");
        assert(f.ctrl.getTreatmentStatus().remainingMs==48000);
        assert(outputFrequency==75 && outputDuty==pctToDuty16(30));
        testClockUs+=48000000; f.ctrl.update();
        assert(f.ctrl.getState()==SystemState::IDLE && outputDuty==0);
    }
    // Configurar fase sin sesión activa no energiza la salida.
    {
        Fixture f;
        f.send(R"({"cmd":"set","phase":90,"wave":4})"); f.ok("set");
        assert(outputDuty==0 && f.ctrl.getState()==SystemState::IDLE);
        f.send(R"({"cmd":"start","duration":1})"); f.ok("started");
        assert(outputPhase==PWM_MAX_DUTY/4);
        f.pause();
        f.send(R"({"cmd":"set","phase":360})"); f.ok("set");
        assert(f.ctrl.getChannelParams(0).phase==0 && outputDuty==0);
        f.send(R"({"cmd":"set","duty":13107,"intensity":20})"); f.ok("set");
        assert(f.ctrl.getChannelParams(0).duty==13107 && outputDuty==0);
    }
    // Cambio completo de modos, límites válidos y canal deshabilitado.
    {
        Fixture f; f.start();
        f.send(R"({"cmd":"set","wave":3,"freq":15,"burst_freq":0,"phase":0})"); f.ok("set");
        assert(rmtEnabled && macroTimerRunning);
        rejected(f,R"({"cmd":"set","freq":50})");
        assert(rmtEnabled && macroTimerRunning);
        f.send(R"({"cmd":"set","wave":4,"intensity":50,"en":true})"); f.ok("set");
        assert(!rmtEnabled && !macroTimerRunning && outputDuty==pctToDuty16(50));
        f.send(R"({"cmd":"set","wave":3,"en":false})"); f.ok("set");
        assert(!rmtEnabled && !macroTimerRunning && outputDuty==0);
        f.pause();
        f.send(R"({"cmd":"resume"})"); f.ok("resumed");
        assert(!rmtEnabled && !macroTimerRunning && outputDuty==0);
    }
    // Todos los presets siguen siendo configuraciones válidas.
    for (int id=0; id<PRESET_COUNT; ++id) {
        Fixture f;
        f.send(std::string(R"({"cmd":"preset","id":)")+std::to_string(id)+"}"); f.ok("preset");
        f.send(R"({"cmd":"start"})"); f.ok("started");
        assert(f.storage.duration==PRESETS[id].sessionMinutes);
        f.send(R"({"cmd":"stop"})"); f.ok("stopped");
    }
    // El controlador también rechaza configuraciones inválidas sin pasar por JSON.
    {
        Fixture f;
        const auto before=f.ctrl.getConfiguration();
        auto invalid=before;
        invalid.channels[0].frequency=0;
        assert(!f.ctrl.applyConfiguration(invalid));
        assert(!f.ctrl.startTreatment(1,&invalid));
        assert(!f.ctrl.startTreatment(0));
        assert(sameConfig(before,f.ctrl.getConfiguration()));
        f.ctrl.setChannelDutyPct(0,-2.0f);
        assert(sameConfig(before,f.ctrl.getConfiguration()));
        assert(!f.ctrl.resumeTreatment());
    }
    {
        Fixture f; f.start(); testClockUs=7000000; f.ctrl.update();
        f.ctrl.stopTreatment("disconnected");
        assert(outputDuty==0 && !rmtEnabled && !macroTimerRunning);
        JsonDocument status; deserializeJson(status,f.ctrl.buildStatusString().c_str());
        assert(std::string(status["stop_reason"].as<const char*>())=="disconnected");
        assert(status["elapsed_sec"].as<int>()==7);
        assert(!f.ctrl.resumeTreatment());
    }
    std::cout << rejectedCases << " rejected commands preserved parameters, session, output and storage.\n";
    std::cout << "Valid commands, presets, pause/resume, timeout and mode transitions: PASS.\n";
}
