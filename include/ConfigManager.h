#include <LittleFS.h>
#include <ArduinoJson.h>
#include <assert.h>

class ConfigManager
{
    const String CONFIG_FILENAME = "/config.json";
    size_t capacity;

public:
    template<typename K, typename V>
    V getValue(const K key, const V defaultValue)
    {
        //File does not exist
        if (!LittleFS.exists(CONFIG_FILENAME))
        {
            Serial.println("[ConfigManager] Config file does not exist, creating...");

            setValue(key, defaultValue);
            return defaultValue;
        }

        //File exists
        File configFile = LittleFS.open(CONFIG_FILENAME, "r");
        assert(configFile);

        DynamicJsonDocument configJSON(capacity);

        const auto deserializeError = deserializeJson(configJSON, configFile);
        serializeJson(configJSON, Serial);

        if (deserializeError)
        {
            configFile.close();

            setValue(key, defaultValue);
            return defaultValue;
        }

        configFile.close();

        return configJSON[key];
    }

    template<typename K, typename V>
    void setValue(const K key, const V value)
    {
        const auto fileExists = LittleFS.exists(CONFIG_FILENAME);

        File configFile = LittleFS.open(CONFIG_FILENAME, "w+");
        assert(configFile);

        DynamicJsonDocument configJSON(capacity);

        //Read existing variables if file exists
        if (fileExists)
        {
            const auto deserializeError = deserializeJson(configJSON, configFile);
            //serializeJson(configJSON, Serial);

            if (deserializeError)
            {
                Serial.print("[ConfigManager] Deserialize error: ");
                Serial.println(deserializeError.c_str());
            }

            configFile.seek(0, SeekCur); //reset file cursor
        }

        //Update JSON
        configJSON[key] = value;

        //Write to file
        serializeJson(configJSON, configFile);

        //Close file
        configFile.close();
    }

    ConfigManager(const size_t capacity = 2048)
    {
        //LittleFS.format();

        this->capacity = capacity;

        if (!LittleFS.begin())
        {
            Serial.println("[ConfigManager] Failed to mount FS");
            return;
        }

        Serial.println("[ConfigManager] Mounted file system");
    }

    ~ConfigManager() = default;
};