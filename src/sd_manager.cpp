#include "sd_manager.h"
#include "pin_config.h"
#include <SD_MMC.h>

static const char* GIF_DIR = "/gifs";

bool sdInit() {
    SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0, SD_D1, SD_D2, SD_D3);
    if (!SD_MMC.begin("/sdcard", false)) {
        Serial.println("SD_MMC mount failed");
        return false;
    }
    Serial.printf("SD Card: %llu MB\n", SD_MMC.cardSize() / (1024 * 1024));

    if (!SD_MMC.exists(GIF_DIR)) {
        SD_MMC.mkdir(GIF_DIR);
    }
    return true;
}

std::vector<String> listGifs() {
    std::vector<String> gifs;
    File dir = SD_MMC.open(GIF_DIR);
    if (!dir || !dir.isDirectory()) return gifs;

    File entry;
    while ((entry = dir.openNextFile())) {
        String name = entry.name();
        if (!entry.isDirectory() && name.endsWith(".gif")) {
            // entry.name() returns full path on ESP32, extract filename
            int slash = name.lastIndexOf('/');
            if (slash >= 0) name = name.substring(slash + 1);
            gifs.push_back(name);
        }
        entry.close();
    }
    dir.close();
    return gifs;
}

bool deleteGif(const String& name) {
    return SD_MMC.remove(gifPath(name));
}

String gifPath(const String& name) {
    return String(GIF_DIR) + "/" + name;
}

uint64_t getCardFreeBytes() {
    return SD_MMC.totalBytes() - SD_MMC.usedBytes();
}

uint64_t getCardTotalBytes() {
    return SD_MMC.totalBytes();
}

static const char* META_PATH = "/gifs/.meta.json";

static String readMetaFile() {
    File f = SD_MMC.open(META_PATH, FILE_READ);
    if (!f) return "{}";
    String content = f.readString();
    f.close();
    return content.length() > 0 ? content : "{}";
}

static void writeMetaFile(const String& content) {
    File f = SD_MMC.open(META_PATH, FILE_WRITE);
    if (f) {
        f.print(content);
        f.close();
    }
}

int getGifRotation(const String& name) {
    String meta = readMetaFile();
    // Find "name":rotation pattern
    String key = "\"" + name + "\":";
    int pos = meta.indexOf(key);
    if (pos < 0) return 0;
    int valStart = pos + key.length();
    int valEnd = valStart;
    while (valEnd < (int)meta.length() && (isDigit(meta[valEnd]) || meta[valEnd] == '-')) valEnd++;
    if (valEnd == valStart) return 0;
    return meta.substring(valStart, valEnd).toInt();
}

void setGifRotation(const String& name, int rotation) {
    // Normalize rotation
    rotation = ((rotation % 360) + 360) % 360;

    String meta = readMetaFile();
    String key = "\"" + name + "\":";
    int pos = meta.indexOf(key);

    if (pos >= 0) {
        // Replace existing value
        int valStart = pos + key.length();
        int valEnd = valStart;
        while (valEnd < (int)meta.length() && (isDigit(meta[valEnd]) || meta[valEnd] == '-')) valEnd++;
        meta = meta.substring(0, valStart) + String(rotation) + meta.substring(valEnd);
    } else {
        // Add new entry
        if (meta == "{}") {
            meta = "{" + key + String(rotation) + "}";
        } else {
            // Insert before closing brace
            int closeBrace = meta.lastIndexOf('}');
            if (closeBrace > 1) {
                meta = meta.substring(0, closeBrace) + "," + key + String(rotation) + "}";
            } else {
                meta = "{" + key + String(rotation) + "}";
            }
        }
    }

    writeMetaFile(meta);
    Serial.printf("Rotation set: %s = %d\n", name.c_str(), rotation);
}
