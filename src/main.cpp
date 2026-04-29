#include <cstring>
#include <iostream>
#include <unordered_map>
#include <vector>
#include <string>
#include <typeinfo>

#include <redscore/platform/file/native_file.h>

#include "library.h"
#include "bugz/custom_lz.hpp"
#include "redscore/platform/app_state.h"
#include "redscore/platform/logger.h"
#include "redscore/platform/buffer/buffer.h"
#include "redscore/platform/file/memory_file.h"
#include "redscore/utils/simple_fileio.h"



#if defined(__clang__) || defined(__GNUC__)
#include <cxxabi.h>
#include <memory>
static std::string type_name(const std::type_info& ti) {
    int status = 0;
    std::unique_ptr<char, void(*)(void*)> res{
        abi::__cxa_demangle(ti.name(), nullptr, nullptr, &status),
        std::free
    };
    return (status == 0) ? res.get() : ti.name();
}
#else
static std::string type_name(const std::type_info& ti) {
    return ti.name(); // MSVC is already readable-ish
}
#endif

struct BZEHeader {
    uint unk;
    uint section_count;
};

struct SectionHeader {
    uint index;
    uint size;
    uint aligned_size;
};

struct Section {
    SectionHeader m_header{};
    IO::Buffer data;

    void read(const SectionHeader &header, const IO::FilePtr &file) {
        m_header = header;

        IO::Buffer compressed_buffer(header.size);
        file->read_exact(compressed_buffer.as_span());
        data.resize(0x280000);
        const auto decompressed =
                custom_lz::lz_decompress(compressed_buffer.data(), data.data());
        data.resize(decompressed.bytes_written);
    }
};

static void print_indent(std::ostream &os, const int indent) {
    for (int i = 0; i < indent; ++i) os.put(' ');
}

template<typename T>
static void print_scalar(std::ostream &os, const T &value) {
    if constexpr (std::is_same_v<T, u8>) {
        os << static_cast<u32>(value) << " (0x"
                << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<u32>(value)
                << std::dec << std::setfill(' ') << ")";
    } else if constexpr (std::is_same_v<T, i8>) {
        const auto v = static_cast<int>(value);
        os << v << " (0x"
                << std::uppercase << std::hex << static_cast<u32>(static_cast<u8>(value))
                << std::dec << ")";
    } else if constexpr (std::is_integral_v<T>) {
        using U = std::make_unsigned_t<T>;
        os << value << " (0x"
                << std::uppercase << std::hex << static_cast<U>(value)
                << std::dec << ")";
    } else {
        os << value;
    }
}

template<size_t N>
static void print_hex_u8_array(std::ostream &os, const u8 (&arr)[N]) {
    os << "[";
    for (size_t i = 0; i < N; ++i) {
        if (i) os << ", ";
        os << "0x"
                << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<u32>(arr[i])
                << std::dec << std::setfill(' ');
    }
    os << "]";
}

template<typename T, size_t N>
static void print_array(std::ostream &os, const T (&arr)[N], int indent);

template<typename T>
static void print_value(std::ostream &os, const T &value, int indent);

template<typename T, size_t N>
static void print_array(std::ostream &os, const T (&arr)[N], int indent) {
    if constexpr (std::is_same_v<T, u8>) {
        print_hex_u8_array(os, arr);
    } else {
        os << "[";
        for (size_t i = 0; i < N; ++i) {
            if (i) os << ", ";
            if constexpr (!std::is_integral_v<T>) {
                os << "\n";
                print_indent(os, indent);
            }
            print_value(os, arr[i], indent);
        }
        if constexpr (!std::is_integral_v<T>) {
            os << "\n";
            print_indent(os, indent-2);
        }
        os << "]";
    }
}

template<typename T, size_t N>
static void print_array(std::ostream &os, const std::array<T, N> &arr, int indent) {
    if constexpr (std::is_same_v<T, u8>) {
        print_hex_u8_array(os, arr);
    } else {
        os << "[";
        for (size_t i = 0; i < N; ++i) {
            if (i) os << ", ";
            if constexpr (!std::is_integral_v<T>) {
                os << "\n";
                print_indent(os, indent);
            }
            print_value(os, arr[i], indent);
        }
        if constexpr (!std::is_integral_v<T>) {
            os << "\n";
            print_indent(os, indent-2);
        }
        os << "]";
    }
}

template<typename T>
static void print_vector(std::ostream &os, const std::vector<T> &vec, const int indent) {
    os << "[\n";
    for (size_t i = 0; i < vec.size(); ++i) {
        print_indent(os, indent + 2);
        os << "[" << i << "] = ";
        print_value(os, vec[i], indent + 2);
        os << "\n";
    }
    print_indent(os, indent);
    os << "]";
}

static void print_field_name(std::ostream &os, const std::string_view name, const int indent) {
    print_indent(os, indent);
    os << name << " = ";
}

static void print_struct_begin(std::ostream &os) {
    os << "{\n";
}

static void print_struct_end(std::ostream &os, const int indent) {
    print_indent(os, indent);
    os << "}";
}

template<typename T>
static void print_value(std::ostream &os, const T &value, int indent) {
    using U = std::remove_cvref_t<T>;

    if constexpr (std::is_integral_v<T>) {
        print_scalar(os, value);
    } else {
        os << type_name(typeid(U)) << ": ";
        value.pretty_print(os, indent);
    }
}

struct PrettyPrintable {
    virtual void pretty_print(std::ostream &os, int indent) const = 0;

    virtual ~PrettyPrintable() = default;
};

template<typename>
struct is_vector : std::false_type {
};

template<typename U, typename Alloc>
struct is_vector<std::vector<U, Alloc> > : std::true_type {
};

template<typename T>
constexpr bool is_vector_v = is_vector<T>::value;

template<typename>
struct is_std_array : std::false_type {
};

template<typename U, std::size_t N>
struct is_std_array<std::array<U, N> > : std::true_type {
};

template<typename T>
constexpr bool is_std_array_v = is_std_array<T>::value;

template<typename T>
static void print_field(std::ostream &os, const std::string_view name, T &value, int indent) {
    using U = std::remove_cvref_t<T>;
    print_field_name(os, name, indent);
    if constexpr (std::is_array_v<U> || is_std_array_v<U>) {
        print_array(os, value, indent + 2);
    } else if constexpr (is_vector_v<U>) {
        print_vector(os, value, indent + 2);
    } else {
        print_value(os, value, indent);
    }
    os << "\n";
}

#define PRINT_FIELD(os, name, indent) print_field(os, #name, name, indent)

struct OffsetAndSize {
    u32 offset;
    u32 size;

    void pretty_print(std::ostream &os, const int indent) const {
        print_struct_begin(os);
        PRINT_FIELD(os, offset, indent + 2);
        PRINT_FIELD(os, size, indent + 2);
        print_struct_end(os, indent);
    }
};

struct Tag3B_unk1 {
    u32 unk0;
    u32 unk1;
    u32 unk2;

    void pretty_print(std::ostream &os, const int indent) const {
        print_struct_begin(os);
        PRINT_FIELD(os, unk0, indent + 2);
        PRINT_FIELD(os, unk1, indent + 2);
        PRINT_FIELD(os, unk2, indent + 2);
        print_struct_end(os, indent);
    }
};

struct Tag3B_unk2 {
    u16 unk[3];

    void pretty_print(std::ostream &os, const int indent) const {
        PRINT_FIELD(os, unk, indent);
    }
};

struct Tag3B_unk3 {
    u16 unk[14];

    void pretty_print(std::ostream &os, const int indent) const {
        PRINT_FIELD(os, unk, indent);
    }
};

struct Tag3B_unk0 {
    u8 a, b, c, d;

    void pretty_print(std::ostream &os, const int indent) const {
        print_struct_begin(os);
        PRINT_FIELD(os, a, indent + 2);
        PRINT_FIELD(os, b, indent + 2);
        PRINT_FIELD(os, c, indent + 2);
        PRINT_FIELD(os, d, indent + 2);
        print_struct_end(os, indent);
    }
};

struct Tag3B {
    std::vector<Tag3B_unk0> unk0;
    std::vector<Tag3B_unk1> unk1;
    std::vector<Tag3B_unk2> unk2;
    std::vector<Tag3B_unk3> unk3;

    void pretty_print(std::ostream &os, const int indent) const {
        print_struct_begin(os);
        PRINT_FIELD(os, unk0, indent + 2);
        PRINT_FIELD(os, unk1, indent + 2);
        PRINT_FIELD(os, unk2, indent + 2);
        PRINT_FIELD(os, unk3, indent + 2);
        print_struct_end(os, indent);
    }
};

struct Localisation {
    std::array<OffsetAndSize, 6> languages;

    void pretty_print(std::ostream &os, const int indent) const {
        print_struct_begin(os);
        PRINT_FIELD(os, languages, indent+2);
        print_struct_end(os, indent);
    }
};

struct LocalisationStrings {
    std::vector<Localisation> localisations;

    void pretty_print(std::ostream &os, const int indent) const {
        print_struct_begin(os);
        PRINT_FIELD(os, localisations, indent + 2);
        print_struct_end(os, indent);
    }
};

struct SubSection {
    u32 offset;
    u32 index;

    void pretty_print(std::ostream &os, const int indent) const {
        print_struct_begin(os);
        PRINT_FIELD(os, offset, indent + 2);
        PRINT_FIELD(os, index, indent + 2);
        print_struct_end(os, indent);
    }
};

struct Section3Resources {
    std::vector<SubSection> sections;

    void pretty_print(std::ostream &os, const int indent) const {
        print_struct_begin(os);
        PRINT_FIELD(os, sections, indent + 2);
        print_struct_end(os, indent);
    }
};

struct Section4Resource2 {
    std::vector<OffsetAndSize> items;

    void pretty_print(std::ostream &os, const int indent) const {
        print_struct_begin(os);
        PRINT_FIELD(os, items, indent + 2);
        print_struct_end(os, indent);
    }
};

struct Tag22_unk3 {
    u32 unk0;
    u32 unk1;
    u32 unk2;

    void pretty_print(std::ostream &os, const int indent) const {
        print_struct_begin(os);
        PRINT_FIELD(os, unk0, indent + 2);
        PRINT_FIELD(os, unk1, indent + 2);
        PRINT_FIELD(os, unk2, indent + 2);
        print_struct_end(os, indent);
    }
};

struct Section4Resource {
    u32 unk0;
    OffsetAndSize offset_and_size;

    void pretty_print(std::ostream &os, const int indent) const {
        print_struct_begin(os);
        PRINT_FIELD(os, unk0, indent + 2);
        PRINT_FIELD(os, offset_and_size, indent + 2);
        print_struct_end(os, indent);
    }
};

struct Tag22_28 {
    u8 data[3];
};

struct Tag22 {
    std::vector<OffsetAndSize> section4_resources;
    std::vector<u32> unk1;
    std::vector<OffsetAndSize> section4_resources2;
    std::vector<Tag22_unk3> unk3;
    std::vector<OffsetAndSize> section4_resources3;
    std::vector<Section4Resource> section4_resources4;
    std::vector<Tag22_28> tag28;

    void pretty_print(std::ostream &os, const int indent) const {
        print_struct_begin(os);
        PRINT_FIELD(os, section4_resources, indent + 2);
        PRINT_FIELD(os, unk1, indent + 2);
        PRINT_FIELD(os, section4_resources2, indent + 2);
        PRINT_FIELD(os, unk3, indent + 2);
        PRINT_FIELD(os, section4_resources3, indent + 2);
        PRINT_FIELD(os, section4_resources4, indent + 2);
        print_struct_end(os, indent);
    }
};

struct Tag20_unk1 {
    u32 unk0;
    u32 unk1;

    void pretty_print(std::ostream &os, const int indent) const {
        print_struct_begin(os);
        PRINT_FIELD(os, unk0, indent + 2);
        PRINT_FIELD(os, unk1, indent + 2);
        print_struct_end(os, indent);
    }
};

struct Tag20_unk2 {
    u16 unk0;
    u16 unk1;
    u16 unk2;

    void pretty_print(std::ostream &os, const int indent) const {
        print_struct_begin(os);
        PRINT_FIELD(os, unk0, indent + 2);
        PRINT_FIELD(os, unk1, indent + 2);
        PRINT_FIELD(os, unk2, indent + 2);
        print_struct_end(os, indent);
    }
};

struct Tag20_unk3 {
    u32 unk0;
    u32 unk1;
    u32 unk2;

    void pretty_print(std::ostream &os, const int indent) const {
        print_struct_begin(os);
        PRINT_FIELD(os, unk0, indent + 2);
        PRINT_FIELD(os, unk1, indent + 2);
        PRINT_FIELD(os, unk2, indent + 2);
        print_struct_end(os, indent);
    }
};

struct Tag20_unk4 {
    u32 unk0;
    u32 unk1;
    u32 unk2;

    void pretty_print(std::ostream &os, const int indent) const {
        print_struct_begin(os);
        PRINT_FIELD(os, unk0, indent + 2);
        PRINT_FIELD(os, unk1, indent + 2);
        PRINT_FIELD(os, unk2, indent + 2);
        print_struct_end(os, indent);
    }
};

struct Tag20 {
    std::vector<u32> unk0;
    std::vector<Tag20_unk1> unk1;
    std::vector<Tag20_unk2> unk2;
    std::vector<Tag20_unk3> unk3;
    std::vector<Tag20_unk4> unk4;
    std::vector<u16> unk5;
    std::vector<u8> unk6;

    void pretty_print(std::ostream &os, const int indent) const {
        print_struct_begin(os);
        PRINT_FIELD(os, unk0, indent + 2);
        PRINT_FIELD(os, unk1, indent + 2);
        PRINT_FIELD(os, unk2, indent + 2);
        PRINT_FIELD(os, unk3, indent + 2);
        PRINT_FIELD(os, unk4, indent + 2);
        PRINT_FIELD(os, unk5, indent + 2);
        PRINT_FIELD(os, unk6, indent + 2);
        print_struct_end(os, indent);
    }
};

struct Tag07_unk1 {
    u32 unk0;
    u32 unk1;
    u32 unk2;

    void pretty_print(std::ostream &os, const int indent) const {
        print_struct_begin(os);
        PRINT_FIELD(os, unk0, indent + 2);
        PRINT_FIELD(os, unk1, indent + 2);
        PRINT_FIELD(os, unk2, indent + 2);
        print_struct_end(os, indent);
    }
};

struct Tag07_unk2 {
    u16 unk0;
    u16 unk1;
    u16 unk2;

    void pretty_print(std::ostream &os, const int indent) const {
        print_struct_begin(os);
        PRINT_FIELD(os, unk0, indent + 2);
        PRINT_FIELD(os, unk1, indent + 2);
        PRINT_FIELD(os, unk2, indent + 2);
        print_struct_end(os, indent);
    }
};

struct Tag07_unk3 {
    u32 unk0;
    u32 unk1;
    u32 unk2;

    void pretty_print(std::ostream &os, const int indent) const {
        print_struct_begin(os);
        PRINT_FIELD(os, unk0, indent + 2);
        PRINT_FIELD(os, unk1, indent + 2);
        PRINT_FIELD(os, unk2, indent + 2);
        print_struct_end(os, indent);
    }
};

struct Tag07_unk6 {
    u32 unk0;
    u32 unk1;

    void pretty_print(std::ostream &os, const int indent) const {
        print_struct_begin(os);
        PRINT_FIELD(os, unk0, indent + 2);
        PRINT_FIELD(os, unk1, indent + 2);
        print_struct_end(os, indent);
    }
};

struct Tag07_unk11 {
    u32 unk[6];

    void pretty_print(std::ostream &os, const int indent) const {
        PRINT_FIELD(os, unk, indent);
    }
};

struct Tag07_unk12 {
    i32 unk0;
    i16 unk1[2];
    i16 unk2[2];

    void pretty_print(std::ostream &os, const int indent) const {
        print_struct_begin(os);
        PRINT_FIELD(os, unk0, indent + 2);
        PRINT_FIELD(os, unk1, indent + 2);
        PRINT_FIELD(os, unk2, indent + 2);
        print_struct_end(os, indent);
    }
};

struct Tag07_unk14 {
    u16 unk0;
    u16 unk1;
    u16 unk2;

    void pretty_print(std::ostream &os, const int indent) const {
        print_struct_begin(os);
        PRINT_FIELD(os, unk0, indent + 2);
        PRINT_FIELD(os, unk1, indent + 2);
        PRINT_FIELD(os, unk2, indent + 2);
        print_struct_end(os, indent);
    }
};

struct Tag07_unk15 {
    u32 unk0;
    u32 unk1;

    void pretty_print(std::ostream &os, const int indent) const {
        print_struct_begin(os);
        PRINT_FIELD(os, unk0, indent + 2);
        PRINT_FIELD(os, unk1, indent + 2);
        print_struct_end(os, indent);
    }
};

struct Tag07_unk18 {
    u32 unk[8];

    void pretty_print(std::ostream &os, const int indent) const {
        PRINT_FIELD(os, unk, indent);
    }
};

struct Tag07_unk19 {
    u32 unk[8];

    void pretty_print(std::ostream &os, const int indent) const {
        PRINT_FIELD(os, unk, indent);
    }
};

struct Tag07_unk23 {
    u16 unk0;
    u16 unk1;
    u16 unk2;

    void pretty_print(std::ostream &os, const int indent) const {
        print_struct_begin(os);
        PRINT_FIELD(os, unk0, indent + 2);
        PRINT_FIELD(os, unk1, indent + 2);
        PRINT_FIELD(os, unk2, indent + 2);
        print_struct_end(os, indent);
    }
};

struct Tag07_35 {
    u8 data[40];
};

struct Tag07 {
    std::vector<u16> unk0;
    std::vector<Tag07_unk1> unk1;
    std::vector<Tag07_unk2> unk2;
    std::vector<Tag07_unk3> unk3;
    std::vector<u16> unk4;
    std::vector<u8> unk5;
    std::vector<Tag07_unk6> unk6;
    std::vector<u32> unk7;
    std::vector<u8> unk8;
    std::vector<u16> unk9;
    std::vector<u16> unk10;
    std::vector<Tag07_unk11> unk11;
    std::vector<Tag07_35> tag35;
    std::vector<Tag07_unk12> unk12;
    std::vector<u8> unk13;
    std::vector<Tag07_unk14> unk14;
    std::vector<Tag07_unk15> unk15;
    std::vector<u16> unk16;
    std::vector<u32> unk17;
    std::vector<Tag07_unk18> unk18;
    std::vector<Tag07_unk19> unk19;
    std::vector<u32> unk20;
    std::vector<u32> unk21;
    std::vector<u32> unk22;
    std::vector<Tag07_unk23> unk23;

    void pretty_print(std::ostream &os, const int indent) const {
        print_struct_begin(os);
        PRINT_FIELD(os, unk0, indent + 2);
        PRINT_FIELD(os, unk1, indent + 2);
        PRINT_FIELD(os, unk2, indent + 2);
        PRINT_FIELD(os, unk3, indent + 2);
        PRINT_FIELD(os, unk4, indent + 2);
        PRINT_FIELD(os, unk5, indent + 2);
        PRINT_FIELD(os, unk6, indent + 2);
        PRINT_FIELD(os, unk7, indent + 2);
        PRINT_FIELD(os, unk8, indent + 2);
        PRINT_FIELD(os, unk9, indent + 2);
        PRINT_FIELD(os, unk10, indent + 2);
        PRINT_FIELD(os, unk11, indent + 2);
        PRINT_FIELD(os, unk12, indent + 2);
        PRINT_FIELD(os, unk13, indent + 2);
        PRINT_FIELD(os, unk14, indent + 2);
        PRINT_FIELD(os, unk15, indent + 2);
        PRINT_FIELD(os, unk16, indent + 2);
        PRINT_FIELD(os, unk17, indent + 2);
        PRINT_FIELD(os, unk18, indent + 2);
        PRINT_FIELD(os, unk19, indent + 2);
        PRINT_FIELD(os, unk20, indent + 2);
        PRINT_FIELD(os, unk21, indent + 2);
        PRINT_FIELD(os, unk22, indent + 2);
        PRINT_FIELD(os, unk23, indent + 2);
        print_struct_end(os, indent);
    }
};

struct Tag00_01 {
    u32 data[2];

    void pretty_print(std::ostream &os, const int indent = 0) const {
        print_struct_begin(os);
        PRINT_FIELD(os, data, indent + 2);
        print_struct_end(os, indent);
    }
};

struct Tag00_02 {
    u32 data[2];

    void pretty_print(std::ostream &os, const int indent = 0) const {
        print_struct_begin(os);
        PRINT_FIELD(os, data, indent + 2);
        print_struct_end(os, indent);
    }
};

struct Tag00_04 {
    u32 data[2];

    void pretty_print(std::ostream &os, const int indent = 0) const {
        print_struct_begin(os);
        PRINT_FIELD(os, data, indent + 2);
        print_struct_end(os, indent);
    }
};

struct Tag00_44 {
    u32 data[3];

    void pretty_print(std::ostream &os, const int indent = 0) const {
        print_struct_begin(os);
        PRINT_FIELD(os, data, indent + 2);
        print_struct_end(os, indent);
    }
};

struct Tag00_4C {
    u32 data[48/4];

    void pretty_print(std::ostream &os, const int indent = 0) const {
        print_struct_begin(os);
        PRINT_FIELD(os, data, indent + 2);
        print_struct_end(os, indent);
    }
};

struct Tag00_4D {
    u32 data[48/4];

    void pretty_print(std::ostream &os, const int indent = 0) const {
        print_struct_begin(os);
        PRINT_FIELD(os, data, indent + 2);
        print_struct_end(os, indent);
    }
};

struct Tag00 {
    std::vector<Tag00_01> tag01;
    std::vector<Tag00_02> tag02;
    std::vector<Tag00_04> tag04;
    std::vector<Tag00_44> tag44;
    std::vector<Tag00_4C> tag4C;
    std::vector<Tag00_4D> tag4D;

    void pretty_print(std::ostream &os, const int indent = 0) const {
        print_struct_begin(os);
        PRINT_FIELD(os, tag01, indent + 2);
        PRINT_FIELD(os, tag02, indent + 2);
        PRINT_FIELD(os, tag04, indent + 2);
        PRINT_FIELD(os, tag44, indent + 2);
        PRINT_FIELD(os, tag4C, indent + 2);
        PRINT_FIELD(os, tag4D, indent + 2);
        print_struct_end(os, indent);
    }
};

struct Tag05_06 {
    u8 data[3];

    void pretty_print(std::ostream &os, const int indent = 0) const {
        print_struct_begin(os);
        PRINT_FIELD(os, data, indent + 2);
        print_struct_end(os, indent);
    }
};

struct Tag05 {
    std::vector<Tag05_06> tag06;

    void pretty_print(std::ostream &os, const int indent = 0) const {
    }
};

struct Tag09_10 {
    u8 data[12];

    void pretty_print(std::ostream &os, const int indent = 0) const {
        print_struct_begin(os);
        PRINT_FIELD(os, data, indent + 2);
        print_struct_end(os, indent);
    }
};

struct Tag09_11 {
    u8 data[6];

    void pretty_print(std::ostream &os, const int indent = 0) const {
        print_struct_begin(os);
        PRINT_FIELD(os, data, indent + 2);
        print_struct_end(os, indent);
    }
};

struct Tag09_16 {
    u8 data[8];

    void pretty_print(std::ostream &os, const int indent = 0) const {
        print_struct_begin(os);
        PRINT_FIELD(os, data, indent + 2);
        print_struct_end(os, indent);
    }
};

struct Tag09_1С {
    u8 data[8];

    void pretty_print(std::ostream &os, const int indent = 0) const {
        print_struct_begin(os);
        PRINT_FIELD(os, data, indent + 2);
        print_struct_end(os, indent);
    }
};

struct Tag09_1F {
    u8 data[1];

    void pretty_print(std::ostream &os, const int indent = 0) const {
        print_struct_begin(os);
        PRINT_FIELD(os, data, indent + 2);
        print_struct_end(os, indent);
    }
};

struct Tag09_33 {
    u8 data[32];

    void pretty_print(std::ostream &os, const int indent = 0) const {
        print_struct_begin(os);
        PRINT_FIELD(os, data, indent + 2);
        print_struct_end(os, indent);
    }
};

struct Tag09 {
    std::vector<Tag09_10> tag10;
    std::vector<Tag09_11> tag11;
    std::vector<Tag09_16> tag16;
    std::vector<Tag09_1С> tag1C;
    std::vector<Tag09_1F> tag1F;
    std::vector<Tag09_33> tag33;

    void pretty_print(std::ostream &os, const int indent = 0) const {
        print_struct_begin(os);
        PRINT_FIELD(os, tag10, indent + 2);
        PRINT_FIELD(os, tag11, indent + 2);
        PRINT_FIELD(os, tag16, indent + 2);
        PRINT_FIELD(os, tag1C, indent + 2);
        PRINT_FIELD(os, tag1F, indent + 2);
        PRINT_FIELD(os, tag33, indent + 2);
        print_struct_end(os, indent);
    }
};

struct Tags {
    std::vector<Tag00> tag00{};
    std::vector<Tag05> tag05{};
    std::vector<Tag07> tag07{};
    std::vector<Tag07> tag08{};
    std::vector<Tag09> tag09{};
    std::vector<Tag07> tag0A{};
    std::vector<Tag20> tag20{};
    std::vector<Tag22> tag22{};
    std::vector<Section3Resources> textures{};
    std::vector<Section4Resource2> section4_resource{};
    std::vector<Tag3B> tag3B{};
    std::vector<LocalisationStrings> tag49{};

    void pretty_print(std::ostream &os, const int indent = 0) const {
        print_struct_begin(os);
        PRINT_FIELD(os, tag00, indent + 2);
        PRINT_FIELD(os, tag05, indent + 2);
        PRINT_FIELD(os, tag07, indent + 2);
        PRINT_FIELD(os, tag08, indent + 2);
        PRINT_FIELD(os, tag09, indent + 2);
        PRINT_FIELD(os, tag0A, indent + 2);
        PRINT_FIELD(os, tag20, indent + 2);
        PRINT_FIELD(os, tag22, indent + 2);
        PRINT_FIELD(os, textures, indent + 2);
        PRINT_FIELD(os, section4_resource, indent + 2);
        PRINT_FIELD(os, tag3B, indent + 2);
        PRINT_FIELD(os, tag49, indent + 2);
        print_struct_end(os, indent);
    }
};

static std::ostream &operator<<(std::ostream &os, const Tags &tags) {
    tags.pretty_print(os, 0);
    return os;
}

void read_bytes(void *dst, const u8 **ctl_data, const u32 size) {
    std::memcpy(dst, *ctl_data, size);
    (*ctl_data) += size;
}

template<typename T>
T &read_one(std::vector<T> &dst, const u8 **ctl_data) {
    auto &slot = dst.emplace_back();
    read_bytes(&slot, ctl_data, sizeof(T));
    return slot;
}

void parse_00_tag(Tag00 &dst, const u8 **ctl_data, const IO::FilePtr &file) {
    u8 tag = **ctl_data;
    (*ctl_data)++;
    while (tag != '.') {
        switch (tag) {
            case (0x01): {
                read_one(dst.tag01, ctl_data);
                break;
            }
            case (0x02): {
                read_one(dst.tag02, ctl_data);
                break;
            }
            case (0x04): {
                read_one(dst.tag04, ctl_data);
                break;
            }
            case (0x44): {
                read_one(dst.tag44, ctl_data);
                break;
            }
            case (0x4C): {
                read_one(dst.tag4C, ctl_data);
                break;
            }
            case (0x4D): {
                read_one(dst.tag4D, ctl_data);
                break;
            }
            default: {
                throw std::runtime_error(std::format("Unsupported 0x00 tag 0x{:02X} (\"{:c}\")", tag, tag));
            }
        }
        tag = **ctl_data;
        if (tag == '.') {
            break;
        }
        (*ctl_data)++;
    }
}

void parse_05_tag(Tag05 &dst, const u8 **ctl_data, const IO::FilePtr &file) {
    u8 tag = **ctl_data;
    (*ctl_data)++;
    while (tag != '.') {
        if (tag == 0x06) {
            read_one(dst.tag06, ctl_data);
        } else {
            throw std::runtime_error(std::format("Unsupported 0x06 tag 0x{:02X} (\"{:c}\")", tag, tag));
        }
        tag = **ctl_data;
        if (tag == '.') {
            break;
        }
        (*ctl_data)++;
    }
}

void parse_09_tag(Tag09 &dst, const u8 **ctl_data, const IO::FilePtr &file) {
    u8 tag = **ctl_data;
    (*ctl_data)++;
    while (tag != '.') {
        switch (tag) {
            case (0x10): {
                read_one(dst.tag10, ctl_data);
                break;
            }
            case (0x11): {
                read_one(dst.tag11, ctl_data);
                break;
            }
            case (0x16): {
                read_one(dst.tag16, ctl_data);
                break;
            }
            case (0x1C): {
                read_one(dst.tag1C, ctl_data);
                break;
            }
            case (0x1F): {
                read_one(dst.tag1F, ctl_data);
                break;
            }
            case (0x33): {
                read_one(dst.tag33, ctl_data);
                break;
            }
            default: {
                throw std::runtime_error(std::format("Unsupported 0x09 tag 0x{:02X} (\"{:c}\")", tag, tag));
            }
        }
        tag = **ctl_data;
        if (tag == '.') {
            break;
        }
        (*ctl_data)++;
    }
}

void parse_07_08_0A_tag(Tag07 &dst, const u8 **ctl_data, const IO::FilePtr &file) {
    u8 tag = **ctl_data;
    (*ctl_data)++;
    while (tag != '.') {
        switch (tag) {
            case (0x0F): {
                read_one(dst.unk0, ctl_data);
                break;
            }
            case (0x10): {
                read_one(dst.unk1, ctl_data);
                break;
            }
            case (0x11): {
                read_one(dst.unk2, ctl_data);
                break;
            }
            case (0x12): {
                read_one(dst.unk3, ctl_data);
                break;
            }
            case (0x38): {
                read_one(dst.unk4, ctl_data);
                break;
            }
            case (0x13): {
                read_one(dst.unk5, ctl_data);
                break;
            }
            case (0x16): {
                read_one(dst.unk6, ctl_data);
                break;
            }
            case (0x27): {
                read_one(dst.unk7, ctl_data);
                break;
            }
            case (0x1F): {
                read_one(dst.unk8, ctl_data);
                break;
            }
            case (0x46): {
                read_one(dst.unk9, ctl_data);
                break;
            }
            case (0x39): {
                read_one(dst.unk10, ctl_data);
                break;
            }
            case (0x34): {
                read_one(dst.unk11, ctl_data);
                break;
            }
            case (0x35): {
                read_one(dst.tag35, ctl_data);
                break;
            }
            case (0x32): {
                read_one(dst.unk12, ctl_data);
                break;
            }
            case (0x15): {
                read_one(dst.unk13, ctl_data);
                break;
            }
            case (0x1B): {
                read_one(dst.unk14, ctl_data);
                break;
            }
            case (0x1C): {
                read_one(dst.unk15, ctl_data);
                break;
            }
            case (0x1E): {
                read_one(dst.unk16, ctl_data);
                break;
            }
            case (0x3A): {
                read_one(dst.unk17, ctl_data);
                break;
            }
            case (0x30): {
                read_one(dst.unk18, ctl_data);
                break;
            }
            case (0x31): {
                read_one(dst.unk19, ctl_data);
                break;
            }
            case (0x1D): {
                read_one(dst.unk20, ctl_data);
                break;
            }
            case (0x0B): {
                read_one(dst.unk21, ctl_data);
                break;
            }
            case (0x0C): {
                read_one(dst.unk22, ctl_data);
                break;
            }
            case (0x42): {
                read_one(dst.unk23, ctl_data);
                break;
            }
            default: {
                throw std::runtime_error(std::format("Unsupported 0x07 tag 0x{:02X} (\"{:c}\")", tag, tag));
            }
        }
        tag = **ctl_data;
        if (tag == '.') {
            break;
        }
        (*ctl_data)++;
    }
}

void parse_20_tag(Tag20 &dst, const u8 **ctl_data, const IO::FilePtr &file) {
    u8 tag = **ctl_data;
    (*ctl_data)++;
    while (tag != '.') {
        switch (tag) {
            case ('!'): {
                read_one(dst.unk0, ctl_data);
                break;
            }
            case ('$'): {
                read_one(dst.unk1, ctl_data);
                break;
            }
            case (0x11): {
                read_one(dst.unk2, ctl_data);
                break;
            }
            case (0x12): {
                read_one(dst.unk3, ctl_data);
                break;
            }
            case (0x10): {
                read_one(dst.unk4, ctl_data);
                break;
            }
            case ('9'): {
                read_one(dst.unk5, ctl_data);
                break;
            }
            case ('C'): {
                read_one(dst.unk6, ctl_data);
                break;
            }
            default: {
                throw std::runtime_error(std::format("Unsupported 0x20 tag 0x{:02X} (\"{:c}\")", tag, tag));
            }
        }
        tag = **ctl_data;
        if (tag == '.') {
            break;
        }
        (*ctl_data)++;
    }
}

void parse_22_tag(Tag22 &dst, const u8 **ctl_data, const IO::FilePtr &file) {
    u8 tag = **ctl_data;
    (*ctl_data)++;
    while (tag != '.') {
        switch (tag) {
            case ('$'): {
                read_one(dst.section4_resources, ctl_data);
                break;
            }
            case ('\''): {
                read_one(dst.unk1, ctl_data);
                break;
            }
            case ('%'): {
                read_one(dst.section4_resources2, ctl_data);
                break;
            }
            case ('\r'): {
                read_one(dst.unk3, ctl_data);
                break;
            }
            case ('@'): {
                read_one(dst.section4_resources3, ctl_data);
                break;
            }
            case ('?'): {
                read_one(dst.section4_resources4, ctl_data);
                break;
            }
            case ('('): {
                read_one(dst.tag28, ctl_data);
                break;
            }
            default: {
                throw std::runtime_error(std::format("Unsupported 0x22 tag 0x{:02X} (\"{:c}\")", tag, tag));
            }
        }
        tag = **ctl_data;
        if (tag == '.') {
            break;
        }
        (*ctl_data)++;
    }
}

void parse_29_tag(Section3Resources &dst, const u8 **ctl_data, const IO::FilePtr &file) {
    u8 tag = **ctl_data;
    (*ctl_data)++;
    while (tag != '.') {
        switch (tag) {
            case ('*'): {
                auto &section = dst.sections.emplace_back();
                read_bytes(&section, ctl_data, sizeof(section));
                break;
            }
            default: {
                throw std::runtime_error(
                    std::format("Unsupported 0x29 tag 0x{:02X} (\"{:c}\")", tag, tag));
            }
        }
        tag = **ctl_data;
        if (tag == '.') {
            break;
        }
        (*ctl_data)++;
    }
}

void parse_36_tag(Section4Resource2 &dst, const u8 **ctl_data, const IO::FilePtr &file) {
    u8 tag = **ctl_data;
    (*ctl_data)++;
    while (tag != '.') {
        switch (tag) {
            case ('7'): {
                auto &item = dst.items.emplace_back();
                read_bytes(&item.offset, ctl_data, sizeof(item.offset));
                read_bytes(&item.size, ctl_data, sizeof(item.size));
                break;
            }

            default: {
                throw std::runtime_error(
                    std::format("Unsupported 0x36 tag 0x{:02X} (\"{:c}\")", tag, tag));
            }
        }
        tag = **ctl_data;
        if (tag == '.') {
            break;
        }
        (*ctl_data)++;
    }
}

void parse_3B_tag(Tag3B &dst, const u8 **ctl_data, const IO::FilePtr &file) {
    u8 tag = **ctl_data;
    (*ctl_data)++;
    while (tag != '.') {
        switch (tag) {
            case (0x3C): {
                read_one(dst.unk0, ctl_data);
                break;
            }
            case (0x2B): {
                read_one(dst.unk1, ctl_data);
                break;
            }
            case (0x3D): {
                auto &res = read_one(dst.unk2, ctl_data);
                res.unk[0] /= 4;
                res.unk[1] /= 4;
                res.unk[2] /= 4;
                break;
            }
            case (0x3E): {
                read_one(dst.unk3, ctl_data);
                break;
            }
            default: {
                throw std::runtime_error(
                    std::format("Unsupported 0x3B tag 0x{:02X} (\"{:c}\")", tag, tag));
            }
        }
        tag = **ctl_data;
        if (tag == '.') {
            break;
        }
        (*ctl_data)++;
    }
}

void parse_49_tag(LocalisationStrings &dst, const u8 **ctl_data, const IO::FilePtr &file) {
    u8 tag = **ctl_data;
    (*ctl_data)++;
    while (tag != '.') {
        switch (tag) {
            case ('J'): {
                read_one(dst.localisations, ctl_data);
                break;
            }
            default: {
                throw std::runtime_error(
                    std::format("Unsupported 0x49 tag 0x{:02X} (\"{:c}\")", tag, tag));
            }
        }
        tag = **ctl_data;
        if (tag == '.') {
            break;
        }
        (*ctl_data)++;
    }
}

struct BZEFile {
    IO::FilePtr m_file;
    BZEHeader header{};
    std::vector<SectionHeader> section_headers;
    u32 active_section = 0;
    std::vector<u8> e_blocks;
    Tags tags;

    std::unordered_map<u32, std::vector<u8> > m_cached_sections;

    BZEFile(IO::FilePtr &&file) : m_file(std::move(file)) {
        header = m_file->read_pod<BZEHeader>();
        section_headers.resize(header.section_count);
        for (int i = 0; i < header.section_count; ++i) {
            section_headers[i] = m_file->read_pod<SectionHeader>();
        }
        m_file->align(2048);

        Section control_section;
        active_section = 0;
        control_section.read(section_headers[0], m_file);
        process_control_section(control_section.data);
    }

    std::optional<IO::Buffer> get_section(const u32 index) {
        if (m_cached_sections.contains(index)) {
            return IO::Buffer::wrap(m_cached_sections.at(index));
        }

        m_file->set_position(2048); // Skip header section;

        for (const auto &section_header: section_headers) {
            if (section_header.index != index) {
                m_file->skip(section_header.aligned_size);
                continue;
            }
            std::vector<u8> compressed(section_header.size);
            m_file->read_exact(compressed);
            std::vector<u8> decompressed(0x280000);
            const auto res = custom_lz::lz_decompress(compressed.data(), decompressed.data());
            decompressed.resize(res.bytes_written);
            m_cached_sections.emplace(index, std::move(decompressed));
            return IO::Buffer::wrap(m_cached_sections.at(index));
        }
        return {};
    }

    void process_control_section(IO::Buffer &control_buffer) {
        const auto *ctl_data = control_buffer.data();
        while (*ctl_data != '/') {
            if (*ctl_data == '-') {
                ctl_data++;
                u8 tag = *ctl_data;
                while (tag != '.') {
                    ctl_data++;
                    parse_tag(tag, &ctl_data, m_file);
                    tag = *ctl_data;
                }
                ctl_data++;
            } else if (*ctl_data == 'K') {
                ctl_data++;
            } else if (*ctl_data == ',') {
                ctl_data++;
                active_section++;
                if (active_section > section_headers.size()) {
                    throw std::runtime_error("Section index out of range");
                }
                m_file->skip(section_headers[active_section].aligned_size);
            } else {
                throw std::runtime_error(std::format("Unknown tag start at {} 0x{:02X} (\"{:c}\")",
                                                     ctl_data - control_buffer.data(), *ctl_data, *ctl_data));
            }
        }
    }

    void parse_tag(u8 tag, const u8 **ctl_data, const IO::FilePtr &file) {
        switch (tag) {
            case (0x45): {
                u8 id = **ctl_data;
                (void) id;
                (*ctl_data)++;
                active_section++;
                if (active_section > section_headers.size()) {
                    throw std::runtime_error("Section index out of range");
                }
                file->skip(section_headers[active_section].aligned_size);
                break;
            }
            case (0x00): {
                parse_00_tag(tags.tag00.emplace_back(), ctl_data, file);
                break;
            }
            case (0x05): {
                parse_05_tag(tags.tag05.emplace_back(), ctl_data, file);
                break;
            }
            case (0x09): {
                parse_09_tag(tags.tag09.emplace_back(), ctl_data, file);
                break;
            }
            case (0x07): {
                parse_07_08_0A_tag(tags.tag07.emplace_back(), ctl_data, file);
                break;
            }
            case (0x08): {
                parse_07_08_0A_tag(tags.tag08.emplace_back(), ctl_data, file);
                break;
            }
            case (0x0A): {
                parse_07_08_0A_tag(tags.tag0A.emplace_back(), ctl_data, file);
                break;
            }
            case (0x20): {
                parse_20_tag(tags.tag20.emplace_back(), ctl_data, file);
                break;
            }
            case (0x22): {
                parse_22_tag(tags.tag22.emplace_back(), ctl_data, file);
                break;
            }
            case (0x29): {
                parse_29_tag(tags.textures.emplace_back(), ctl_data, file);
                break;
            }
            case (0x3B): {
                parse_3B_tag(tags.tag3B.emplace_back(), ctl_data, file);
                break;
            }
            case (0x36): {
                parse_36_tag(tags.section4_resource.emplace_back(), ctl_data, file);
                break;
            }
            case (0x49): {
                parse_49_tag(tags.tag49.emplace_back(), ctl_data, file);
                break;
            }
            default: {
                throw std::runtime_error(
                    std::format("Unsupported tag 0x{:02X} (\"{:c}\")", tag, tag));
            }
        }
        fflush(stdout);
    }
};

struct ResourceHeader {
    u32 unk0;
    u32 unk1;

    void pretty_print(std::ostream &os, const int indent) const {
        print_struct_begin(os);
        PRINT_FIELD(os, unk0, indent + 2);
        PRINT_FIELD(os, unk1, indent + 2);
        print_struct_end(os, indent);
    }
};

struct ImageHeader {
    u32 size;
    u32 unk2;
    u16 width;
    u16 height;

    void pretty_print(std::ostream &os, const int indent) const {
        print_struct_begin(os);
        PRINT_FIELD(os, size, indent + 2);
        PRINT_FIELD(os, unk2, indent + 2);
        PRINT_FIELD(os, width, indent + 2);
        PRINT_FIELD(os, height, indent + 2);
        print_struct_end(os, indent);
    }
};

void export_image(const std::filesystem::path &orig_file, const SubSection &section, IO::Buffer &sec3) {
    const auto resource_info = sec3.reinterpret_at<ResourceHeader>(section.offset);
    const auto palette_info = sec3.reinterpret_at<ImageHeader>(section.offset + sizeof(ResourceHeader));
    const auto image_offset = section.offset + palette_info->size + sizeof(ResourceHeader);
    const auto image_info = sec3.reinterpret_at<ImageHeader>(image_offset);
    auto &os = std::cout;
    section.pretty_print(os, 0);
    os << "\n";
    print_struct_begin(os);
    print_field_name(os, "resource", 4);
    print_value(os, *resource_info, 4);
    print_field_name(os, "palette", 4);
    print_value(os, *palette_info, 4);
    print_field_name(os, "image", 4);
    print_value(os, *image_info, 4);
    print_struct_end(os, 0);
    os << "\n===========\n";

    const auto &palette_data = sec3.readonly_view(section.offset + sizeof(ResourceHeader) + sizeof(ImageHeader),
                                                  palette_info->size - sizeof(ImageHeader));
    const auto &image_data = sec3.readonly_view(image_offset + sizeof(ImageHeader),
                                                image_info->size - sizeof(ImageHeader));

    PNGFile png_file;
    const u32 width = image_info->width * 2;
    const u32 height = image_info->height;
    IO::Buffer decoded_image(width * height * 4); // RGBA data
    auto rgba_data = decoded_image.writable_view_as<RGBA>();
    auto color_palette = palette_data.readonly_view_as<u16>();

    auto decode_rgba5551 = [](const uint16_t be) -> std::array<u8, 4> {
        // uint16_t v = static_cast<uint16_t>((be >> 8) | (be << 8));
        const u16 v = be;

        const u8 a1 = (v >> 15) & 0x01;
        const u8 b5 = (v >> 10) & 0x1F;
        const u8 g5 = (v >> 5) & 0x1F;
        const u8 r5 = v & 0x1F;

        const u8 a8 = a1 ? 0 : 255;
        const u8 r8 = static_cast<uint8_t>((r5 * 527 + 23) >> 6);
        const u8 g8 = static_cast<uint8_t>((g5 * 527 + 23) >> 6);
        const u8 b8 = static_cast<uint8_t>((b5 * 527 + 23) >> 6);

        return {r8, g8, b8, 0xFF};
    };

    if (resource_info->unk1 == 9) {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const u32 linear_index = y * width + x;
                const u8 color_index = image_data[linear_index];
                const auto &color = decode_rgba5551(color_palette[color_index]);
                rgba_data[linear_index] = {color[0], color[1], color[2], color[3]};
            }
        }
    } else if (resource_info->unk1 == 8) {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; x++) {
                const u32 linear_index = y * width + x;
                const u8 color_index = image_data[linear_index];
                const u8 c0 = color_index & 0x0F;
                const u8 c1 = color_index >> 4;
                const auto &color0 = decode_rgba5551(color_palette[c0]);
                rgba_data[linear_index + 0] = {color0[0], color0[1], color0[2], color0[3]};
                const auto &color1 = decode_rgba5551(color_palette[c1]);
                rgba_data[linear_index + 1] = {color1[0], color1[1], color1[2], color0[3]};
            }
        }
    } else {
        std::cout << "Unsupported type " << resource_info->unk1 << "\n";
    }

    png_from_data(decoded_image.data(), decoded_image.size(), width, height, 4, 8, &png_file);
    IO::MemoryFile png_buffer(64);
    auto read_func = [](UserIO *u, void *out, const u32 size)-> u32 {
        auto *f = static_cast<IO::MemoryFile *>(u->user_file);
        return f->read(out, size);
    };
    auto write_func = [](UserIO *u, const void *in, const u32 size)-> u32 {
        auto *f = static_cast<IO::MemoryFile *>(u->user_file);
        return f->write(in, size);
    };
    UserIO user_io = {read_func, write_func, &png_buffer};
    PNGWriteConfig cfg;
    PNGWriteConfig_default(&cfg);
    png_write(&user_io, &cfg, &png_file);

    std::filesystem::path png_out_path = orig_file;
    png_out_path.replace_filename(std::format("{}_{}.png", orig_file.stem().string(), section.index));
    std::cout << png_out_path;
    write_file(png_out_path, png_buffer.buffer());
    png_free(&png_file);
}


void export_languages(const std::filesystem::path &orig_file, const LocalisationStrings &languages_tag,
                      IO::Buffer &sec3) {
    std::vector<std::array<std::vector<std::string>, 6> > languages;
    for (const auto &localisation: languages_tag.localisations) {
        u32 language_id = 0;
        auto &localization = languages.emplace_back();
        for (const auto &unk: localisation.languages) {
            auto &language = localization[language_id];
            const auto language_buffer = sec3.readonly_view(unk.offset, unk.size);
            IO::MemoryViewFile language_file(language_buffer);
            const auto table_size = language_file.read_pod<u16>();
            const auto string_count = table_size / 2;
            language_file.set_position(table_size, std::ios::beg);
            language.reserve(string_count);
            for (int i = 0; i < string_count; ++i) {
                language.emplace_back(language_file.read_cstring());
            }
            language_id++;
        }
    }

    u32 localization_id = 0;
    for (const auto &localization: languages) {
        std::filesystem::path localization_path = orig_file;

        u32 language_id = 0;
        for (const auto &language: localization) {
            localization_path.replace_filename(std::format("{}_localization_{}_{}.txt", orig_file.stem().string(),
                                                           localization_id, language_id));

            std::ofstream out_file(localization_path);
            for (const auto &string: language) {
                out_file << string << "\n";
            }
            out_file.close();
            language_id++;
        }
    }
}

void export_model(const IO::Buffer &sec4, const OffsetAndSize &section4_resource, GltfHelper &helper,
                  const std::filesystem::path &path) {
    const auto resource_data = sec4.readonly_view(section4_resource.offset, section4_resource.size);

    const auto ident = resource_data.reinterpret_at<u32>(0);
    if (*ident != 0x41) {
        GLog_Info("Resource @ {} in section 4 is not a model", section4_resource.offset);
        return;
    }
    const auto mesh_count = resource_data.reinterpret_at<u32>(8);
    if (*mesh_count == 0) {
        GLog_Info("Model @ {} in section 4 has 0 mehses", section4_resource.offset);
        return;
    }

    auto mesh_data = resource_data.subview(12);
    IO::MemoryViewFile reader(mesh_data);
    struct MeshEntry {
        u32 vertex_offset;
        u32 vertex_count;
        u32 unk2;
        u32 unk_offset;
        u32 primitive_offset;
        u32 primitive_count;
        u32 unk6;
    };

    // u32 global_vertex_count = 0;
    std::vector<MeshEntry> meshes(*mesh_count);
    reader.read_exact(meshes);
    // for (const auto & mesh : meshes) {
    //     global_vertex_count+=mesh.vertex_count;
    // }
    // const auto vertex_buffer = mesh_data.subview(meshes[0].vertex_offset,global_vertex_count*16).readonly_view_as<u32>();
    // std::unordered_map<u16, u32> vertex_map(global_vertex_count);
    // for (int i = 0; i < global_vertex_count; ++i) {
    //     const auto stored_id = vertex_buffer[i*4+3];
    //     vertex_map[stored_id] = i;
    // }

    struct Vertex {
        glm::vec3 pos;
        u32 index;
    };

    struct Prim78 {
        u16 unk[2];
        RGBA color[4];
        u16 indices[4];
    };
    static_assert(sizeof(Prim78) == 28);

    struct Prim74 {
        u16 unk[2];
        RGBA color[3];
        u16 indices[3];
        u16 pad;
    };
    static_assert(sizeof(Prim74) == 24);

    struct Prim64 {
        u32 unk[2];
        RGBA color[5];
        u16 indices[4];
    };
    static_assert(sizeof(Prim64) == 36);

    struct Prim60 {
        u16 unk0;
        u16 i0;
        u16 unk1;
        u16 i1;
        u16 unk2;
        u16 i2;
        RGBA color[3];
        u16 unk[2];
    };
    static_assert(sizeof(Prim60) == 28);

    struct Prim56 {
        RGBA color[4];
        u16 indices[4];
    };
    static_assert(sizeof(Prim56) == 24);


    for (const auto &mesh: meshes) {
        const auto vertex_buffer = mesh_data.subview(mesh.vertex_offset, mesh.vertex_count * sizeof(Vertex)).
                readonly_view_as<Vertex>();
        auto primitive_reader = IO::MemoryViewFile(mesh_data.subview(mesh.primitive_offset));

        std::vector<glm::vec3> positions;
        positions.reserve(mesh.vertex_count);
        std::unordered_map<u16, u16> vertex_map(mesh.vertex_count);

        for (int i = 0; i < mesh.vertex_count; ++i) {
            const auto &[pos, index] = vertex_buffer[i];
            positions.emplace_back(pos * 0.1f);
            vertex_map[index & (~0x8000)] = i;
        }

        auto is_valid_poly = [](const u16 a, const u16 b, const u16 c, const u16 d = 0xFFFF) {
            if (d == 0xFFFF)
                return a != b && a != c && b != c;
            return a != b && a != c && b != c && b != d && a != d;
        };

        std::vector<u16> indices;
        for (int i = 0; i < mesh.primitive_count; ++i) {
            const u16 prim_id = primitive_reader.read_pod<u16>();
            const u8 unk = primitive_reader.read_pod<u8>();
            const u8 prim_type = primitive_reader.read_pod<u8>();
            // GLog_Info("Mesh {:02} Prim {:02} {:02} prim_id: {:02} {:02}", mesh_id, i, unk, prim_id, prim_type);
            switch (prim_type) {
                case 74: {
                    const auto prim = primitive_reader.read_pod<Prim74>();
                    assert(prim.pad==0);
                    assert(is_valid_poly(prim.indices[0],prim.indices[1], prim.indices[2]));
                    indices.emplace_back(vertex_map.at(prim.indices[0]));
                    indices.emplace_back(vertex_map.at(prim.indices[1]));
                    indices.emplace_back(vertex_map.at(prim.indices[2]));
                    break;
                }
                case 78: {
                    const auto prim = primitive_reader.read_pod<Prim78>();
                    assert(is_valid_poly(prim.indices[0],prim.indices[1], prim.indices[2], prim.indices[3]));
                    indices.emplace_back(vertex_map.at(prim.indices[0]));
                    indices.emplace_back(vertex_map.at(prim.indices[1]));
                    indices.emplace_back(vertex_map.at(prim.indices[2]));
                    indices.emplace_back(vertex_map.at(prim.indices[1]));
                    indices.emplace_back(vertex_map.at(prim.indices[2]));
                    indices.emplace_back(vertex_map.at(prim.indices[3]));
                    break;
                }
                case 64: {
                    const auto prim = primitive_reader.read_pod<Prim64>();
                    assert(is_valid_poly(prim.indices[0],prim.indices[1], prim.indices[2], prim.indices[3]));
                    indices.emplace_back(vertex_map.at(prim.indices[0]));
                    indices.emplace_back(vertex_map.at(prim.indices[2]));
                    indices.emplace_back(vertex_map.at(prim.indices[1]));
                    indices.emplace_back(vertex_map.at(prim.indices[1]));
                    indices.emplace_back(vertex_map.at(prim.indices[2]));
                    indices.emplace_back(vertex_map.at(prim.indices[3]));
                    break;
                }
                case 60: {
                    const auto prim = primitive_reader.read_pod<Prim60>();
                    assert(is_valid_poly(prim.i0,prim.i1, prim.i2));
                    // indices.emplace_back(vertex_map.at(prim.i0));
                    // indices.emplace_back(vertex_map.at(prim.i1));
                    // indices.emplace_back(vertex_map.at(prim.i2));
                    break;
                }
                case 56: {
                    const auto prim = primitive_reader.read_pod<Prim56>();
                    assert(is_valid_poly(prim.indices[0],prim.indices[1], prim.indices[2], prim.indices[3]));
                    indices.emplace_back(vertex_map.at(prim.indices[0]));
                    indices.emplace_back(vertex_map.at(prim.indices[2]));
                    indices.emplace_back(vertex_map.at(prim.indices[1]));
                    indices.emplace_back(vertex_map.at(prim.indices[1]));
                    indices.emplace_back(vertex_map.at(prim.indices[2]));
                    indices.emplace_back(vertex_map.at(prim.indices[3]));
                    break;
                }
                default: {
                    GLog_Error("Unsupported primitive type {} @ {}", prim_type,
                               section4_resource.offset+ 12 + mesh.primitive_offset + primitive_reader.
                               get_position(
                               ));
                    throw std::runtime_error("Unsuported primitive");
                }
            }
        }

        if (indices.empty()) {
            continue;
        }
        const auto mesh_node = helper.make<tinygltf::Node>();
        helper.add_to_scene(mesh_node);
        const auto gltf_mesh = helper.make<tinygltf::Mesh>();
        mesh_node->mesh = gltf_mesh.index();

        auto &primitive = gltf_mesh->primitives.emplace_back();
        primitive.mode = TINYGLTF_MODE_TRIANGLES;
        helper.set_primitive_indices(primitive, reinterpret_cast<u8 *>(indices.data()),
                                     indices.size() * sizeof(u16),
                                     TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT, indices.size(), 0, 0, "Indices");
        helper.set_primitive_attribute(primitive, "POSITION", reinterpret_cast<u8 *>(positions.data()),
                                       positions.size() * sizeof(glm::vec3),
                                       TINYGLTF_COMPONENT_TYPE_FLOAT, TINYGLTF_TYPE_VEC3, positions.size(),
                                       false, 0, 0, "positions"
        );
    }

    const auto gltf_model = helper.model();

    tinygltf::TinyGLTF writer;
    std::filesystem::path gltf_output_path = path;
    gltf_output_path.replace_extension("gltf");
    writer.WriteGltfSceneToFile(&gltf_model, gltf_output_path.string(), true, true, true, false);

    GLog_Info("A");
}

int main() {
    const auto path = std::filesystem::path(
        "/home/red_eye/Trash/Bugs Bunny - Lost in Time/bze/L03ACOM.bze");
    IO::FilePtr file = IO::open_file(path);

    BZEFile bze_file(std::move(file));
    std::cout << bze_file.tags << std::endl;

    bze_file.m_file->set_position(2048);
    for (const auto &section_header: bze_file.section_headers) {
        const auto data = bze_file.m_file->read_exact<u8>(section_header.size);
        std::filesystem::path output_path = path;
        output_path.replace_extension(std::format("sec_{}", section_header.index));
        std::vector<u8> decompressed(0x280000);
        const auto res = custom_lz::lz_decompress(data.data(), decompressed.data());
        decompressed.resize(res.bytes_written);
        write_file(output_path, decompressed);
        bze_file.m_file->align(2048);
    }


    auto sec3 = bze_file.get_section(3);
    auto sec4 = bze_file.get_section(4);
    // for (const auto &section: bze_file.tags.textures.sections) {
    //     export_image(path, section, sec3);
    // }
    // export_languages(path, bze_file.tags.tag49, sec3);

    // AppState app(path.parent_path().parent_path());
    // for (const auto& tag22: bze_file.tags.tag22) {
    //     for (const auto &section4_resource: tag22.section4_resources) {
    //         export_model(sec4, section4_resource, app.helper(), path);
    //     }
    // }

    std::cout << "Hello, World!" << std::endl;
    return 0;
}
