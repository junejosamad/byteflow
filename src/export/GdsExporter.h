#pragma once
#include <string>
#include <vector>
#include <fstream>

class Design; // Forward declaration

class GdsExporter {
public:
    static bool exportGds(const std::string& filename, Design* design);

private:
    // GDSII Record Types (Hex)
    static const uint16_t HEADER    = 0x0002;
    static const uint16_t BGNLIB    = 0x0102;
    static const uint16_t LIBNAME   = 0x0206;
    static const uint16_t UNITS     = 0x0305;
    static const uint16_t ENDLIB    = 0x0400;
    static const uint16_t BGNSTR    = 0x0502;
    static const uint16_t STRNAME   = 0x0606;
    static const uint16_t ENDSTR    = 0x0700;
    static const uint16_t BOUNDARY  = 0x0800;
    static const uint16_t LAYER     = 0x0D02;
    static const uint16_t DATATYPE  = 0x0E02;
    static const uint16_t XY        = 0x1003;
    static const uint16_t ENDEL     = 0x1100;

    // Endian Swapping Helpers (CRITICAL for x86 machines)
    static uint16_t swap16(uint16_t val);
    static uint32_t swap32(uint32_t val);
    
    // GDSII floating point format (IBM System/360 8-byte floating point format)
    static uint64_t real8ToGDS(double value);
    
    // Binary Write Helpers
    static void writeRecordHeader(std::ofstream& out, uint16_t size, uint16_t recordType);
    static void writeI2(std::ofstream& out, uint16_t recordType, int16_t val);
    static void writeI4(std::ofstream& out, uint16_t recordType, int32_t val);
    static void writeReal8(std::ofstream& out, uint16_t recordType, double val);
    static void writeString(std::ofstream& out, uint16_t recordType, const std::string& str);
    
    static void writePolygon(std::ofstream& out, int layer, const std::vector<std::pair<int, int>>& points);
};
