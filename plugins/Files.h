#include <libstuff/libstuff.h>
#include "../BedrockPlugin.h"

// Declare the class we're going to implement below
class BedrockPlugin_Files : public BedrockPlugin {
  public:
    // Constructor / Destructor
    BedrockPlugin_Files();
    ~BedrockPlugin_Files();

    // Implement base class interface
    virtual string getName() { return "Files"; }
    virtual void initialize(const SData& args, BedrockServer& server);
    virtual void upgradeDatabase(SQLite& db);
    virtual bool peekCommand(SQLite& db, BedrockCommand& command);
    virtual bool processCommand(SQLite& db, BedrockCommand& command);

private:
    // Methods
    bool deleteFile(const string& filePath);
    bool makeDirectory(const string& path);
    bool directoryExists(const string& path);
    bool isDirectoryEmpty(const string& dirname);
    string trim(const string& str, const string& chars);
    vector<string> split(const string& str, const string& delim);

    // Constants
    const string _filesPath;

};
