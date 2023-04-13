#include "jimi_util.h"

const char * decode_alarm_code(uint8_t code) {
    switch ( code) {
        case 0:
            return "Normal";

        case 0x1:
            return "SOS alert";

        case 0x2:
            return "Power cut";

        case 0x3:
            return "Vibration";

        case 0x4:
            return "Fence entered";

        case 0x5:
            return "Fence left";

        case 0x6:
            return "Speeding";

        case 0x9:
            return "Tow/theft";

        case 0xA:
            return "Entered GPS blind spot";

        case 0xB:
            return "Left GPS blind spot";

        case 0xC:
            return "Power on";

        case 0xD:
            return "GPs first fix";

        case 0xE:
            return "Low battery";

        case 0xF:
            return "Low voltage protection";

        case 0x10:
            return "SIM changed";

        case 0x11:
            return "Powered off";

        case 0x12:
            return "Airplane mode because battery low voltage";

        case 0x13:
            return "Tamper alert";

        case 0x14:
            return "Door alert";

        case 0x15:
            return "Low battery power off";

        case 0x16:
            return "Sound control alert";

        case 0x17:
            return "Rogue base station detected";

        case 0x18:
            return "Cover removed";

        case 0x19:
            return "Low internal battery";

        case 0x1A:
            return "Exited transit mode";

        case 0x1B:
            return "Leaving the herd";

        case 0x20:
            return "Entered deep sleep";

        case 0x23:
            return "Fall alert";

        case 0x24:
            return "Charger connected";

        case 0x25:
            return "Light detected";

        case 0x27:
            return "Wire cut";

        case 0x28:
            return "Solicited offline";

        case 0x29:
            return "Harsh acceleration";

        case 0x2A:
            return "Sharp left turn";

        case 0x2B:
            return "Sharp right turn";

        case 0x2C:
            return "Collision";

        case 0x30:
            return "Harsh braking";

        case 0x31:
            return "Left the herd";

        case 0x33:
            return "Locked";

        case 0x34:
            return "Unlocked";

        case 0x35:
            return "Illegally unlocked";

        case 0x36:
            return "Unlock failed";

        case 0x37:
            return "Knocking";

        case 0x3A:
            return "Anklet removed";

        case 0x52:
            return "Temperature exception";

        case 0x64:
            "Temperature normal"
            ;

        case 0x65:
            return "Device being violently damaged";

        case 0xFF:
            return "ACC OFF";

        case 0xFE:
            return "ACC ON";

        default:
            return "Unknown";
    }
}
