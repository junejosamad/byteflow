#include "GdsExporter.h"
#include "db/Design.h"
#include <iostream>
#include <cmath>

uint16_t GdsExporter::swap16(uint16_t val) {
    return (val << 8) | (val >> 8);
}

uint32_t GdsExporter::swap32(uint32_t val) {
    return ((val << 24) & 0xFF000000) |
           ((val << 8) & 0x00FF0000) |
           ((val >> 8) & 0x0000FF00) |
           ((val >> 24) & 0x000000FF);
}

// GDSII requires 8-byte IBM System/360 floating-point format
uint64_t GdsExporter::real8ToGDS(double value) {
    if (value == 0.0) return 0;
    
    // IEEE 754 to IBM Hexadecimal conversion
    uint64_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    
    int sign = (bits >> 63) & 1;
    int exp = ((bits >> 52) & 0x7FF) - 1023; // Unbias IEEE exponent
    uint64_t mantissa = (bits & 0xFFFFFFFFFFFFF) | 0x10000000000000; // Add hidden 1
    
    // Convert base-2 exponent to base-16
    exp += 4; // adjust 16-bit boundaries
    int exp16 = (exp + 256) / 4; // Bias IBM Exponent by 64 (x4 = 256)
    int shift = (exp16 * 4) - exp - 4; // Shift amount to align hex boundary
    
    mantissa >>= shift;
    
    uint64_t result = ((uint64_t)sign << 63) | ((uint64_t)(exp16 & 0x7F) << 56) | (mantissa >> 4);
    
    // Swap 64 bit endianness
    return ((result & 0xFF00000000000000ULL) >> 56) | 
           ((result & 0x00FF000000000000ULL) >> 40) | 
           ((result & 0x0000FF0000000000ULL) >> 24) | 
           ((result & 0x000000FF00000000ULL) >> 8) | 
           ((result & 0x00000000FF000000ULL) << 8) | 
           ((result & 0x0000000000FF0000ULL) << 24) | 
           ((result & 0x000000000000FF00ULL) << 40) | 
           ((result & 0x00000000000000FFULL) << 56);
}

void GdsExporter::writeRecordHeader(std::ofstream& out, uint16_t size, uint16_t recordType) {
    uint16_t swappedSize = swap16(size);
    uint16_t swappedType = swap16(recordType);
    out.write(reinterpret_cast<const char*>(&swappedSize), 2);
    out.write(reinterpret_cast<const char*>(&swappedType), 2);
}

void GdsExporter::writeI2(std::ofstream& out, uint16_t recordType, int16_t val) {
    writeRecordHeader(out, 6, recordType);
    uint16_t swappedVal = swap16(val);
    out.write(reinterpret_cast<const char*>(&swappedVal), 2);
}

void GdsExporter::writeI4(std::ofstream& out, uint16_t recordType, int32_t val) {
    writeRecordHeader(out, 8, recordType);
    uint32_t swappedVal = swap32(val);
    out.write(reinterpret_cast<const char*>(&swappedVal), 4);
}

void GdsExporter::writeReal8(std::ofstream& out, uint16_t recordType, double val) {
    writeRecordHeader(out, 12, recordType);
    uint64_t ibmFloat = real8ToGDS(val);
    out.write(reinterpret_cast<const char*>(&ibmFloat), 8);
}

void GdsExporter::writeString(std::ofstream& out, uint16_t recordType, const std::string& str) {
    // Strings in GDSII must be even length
    std::string s = str;
    if (s.length() % 2 != 0) s += '\0';
    
    writeRecordHeader(out, 4 + s.length(), recordType);
    out.write(s.c_str(), s.length());
}

void GdsExporter::writePolygon(std::ofstream& out, int layer, const std::vector<std::pair<int, int>>& points) {
    writeRecordHeader(out, 4, BOUNDARY);
    writeI2(out, LAYER, layer);
    writeI2(out, DATATYPE, 0); // Datatype 0
    
    // Write XY Coordinates
    // Record size = length of header (4) + (num_points * 8 bytes for 2 I4s)
    uint16_t xySize = 4 + (points.size() * 8);
    writeRecordHeader(out, xySize, XY);
    
    for (const auto& pt : points) {
        uint32_t x = swap32(pt.first);
        uint32_t y = swap32(pt.second);
        out.write(reinterpret_cast<const char*>(&x), 4);
        out.write(reinterpret_cast<const char*>(&y), 4);
    }
    
    writeRecordHeader(out, 4, ENDEL);
}

bool GdsExporter::exportGds(const std::string& filename, Design* design) {
    std::ofstream out(filename, std::ios::binary);
    if (!out) {
        std::cerr << "Error: Could not open " << filename << " for writing GDSII." << std::endl;
        return false;
    }
    
    std::cout << "=== EXPORTING GDSII ===" << std::endl;
    std::cout << "  Generating byte-swapped big-endian binary..." << std::endl;

    // 1. Initial Headers
    writeI2(out, HEADER, 600); // GDSII version 6.0
    
    // BGNLIB (Contains 28 bytes of date/time, we just use 0 padding for simplicity)
    writeRecordHeader(out, 28, BGNLIB);
    for (int i = 0; i < 12; ++i) { uint16_t pad = 0; out.write(reinterpret_cast<const char*>(&pad), 2); }
    
    writeString(out, LIBNAME, "OPENEDA_LIB");
    
    // Units: 1 database unit = 1 nm = 0.001 um = 1e-9 m
    writeRecordHeader(out, 20, UNITS);
    uint64_t userUnits = real8ToGDS(0.001); // User unit to meters
    uint64_t dbUnits = real8ToGDS(1e-9);    // Database unit to meters
    out.write(reinterpret_cast<const char*>(&userUnits), 8);
    out.write(reinterpret_cast<const char*>(&dbUnits), 8);
    
    // 2. Start Structure (The Cell)
    writeRecordHeader(out, 28, BGNSTR);
    for (int i = 0; i < 12; ++i) { uint16_t pad = 0; out.write(reinterpret_cast<const char*>(&pad), 2); }
    
    writeString(out, STRNAME, "TOP"); // Name of the top cell
    
    // --- 3. EXPORT STANDARD CELLS ---
    for (auto* inst : design->instances) {
        // Expand gate into 2D Boundary for Layer 10 (Cell Bounding Box)
        int x1 = inst->x;
        int y1 = inst->y;
        int x2 = inst->x + (inst->type->width * 1000); // Scale up to DB bounds roughly
        int y2 = inst->y + (inst->type->height * 1000);
        
        std::vector<std::pair<int, int>> poly = {
            {x1, y1}, {x1, y2}, {x2, y2}, {x2, y1}, {x1, y1} // Close polygon
        };
        writePolygon(out, 10, poly);
    }
    
    // --- 4. EXPORT ROUTING CENTERLINES TO RECTANGLES ---
    int half_width = 10; // Wire width logic
    
    for (auto* net : design->nets) {
        if (net->routePath.size() < 2) continue;
        
        for (size_t i = 0; i < net->routePath.size() - 1; ++i) {
            auto& p1 = net->routePath[i];
            auto& p2 = net->routePath[i+1];
            
            // Layer change = Via (Square polygon)
            if (p1.layer != p2.layer) {
                int vx = p2.x;
                int vy = p2.y;
                int vl = 50; // VIA layer
                
                std::vector<std::pair<int, int>> poly = {
                    {vx - half_width, vy - half_width},
                    {vx - half_width, vy + half_width},
                    {vx + half_width, vy + half_width},
                    {vx + half_width, vy - half_width},
                    {vx - half_width, vy - half_width}
                };
                writePolygon(out, vl, poly);
                continue;
            }
            
            // Wire logic
            std::vector<std::pair<int, int>> poly;
            if (p1.y == p2.y) { // Horizontal
                int left = std::min(p1.x, p2.x);
                int right = std::max(p1.x, p2.x);
                poly.push_back({left, p1.y - half_width});
                poly.push_back({left, p1.y + half_width});
                poly.push_back({right, p1.y + half_width});
                poly.push_back({right, p1.y - half_width});
                poly.push_back({left, p1.y - half_width});
            } else { // Vertical
                int bot = std::min(p1.y, p2.y);
                int top = std::max(p1.y, p2.y);
                poly.push_back({p1.x - half_width, bot});
                poly.push_back({p1.x - half_width, top});
                poly.push_back({p1.x + half_width, top});
                poly.push_back({p1.x + half_width, bot});
                poly.push_back({p1.x - half_width, bot});
            }
            
            writePolygon(out, p1.layer, poly);
        }
    }
    
    // 5. Cleanup
    writeRecordHeader(out, 4, ENDSTR);
    writeRecordHeader(out, 4, ENDLIB);
    
    out.close();
    std::cout << "  Successfully wrote layout to " << filename << std::endl;
    return true;
}
