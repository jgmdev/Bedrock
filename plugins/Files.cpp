#include "Files.h"
#include <dirent.h>
#include <sys/stat.h>

#undef SLOGPREFIX
#define SLOGPREFIX "{" << getName() << "} "

// ==========================================================================
BedrockPlugin_Files::BedrockPlugin_Files()
    : _filesPath("") // Will be set inside initialize()
{
    // Nothing to initialize
}

// ==========================================================================
BedrockPlugin_Files::~BedrockPlugin_Files() {
    // Nothing to clean up
}

// ==========================================================================
void BedrockPlugin_Files::initialize(const SData& args, BedrockServer& server) {
    // Check the configuration
    string filesPath = args["-files.path"];

    if (filesPath.empty()) {
        // Provide a default
        SINFO("No -files.path specified, defaulting to /var/cache/bedrock/files");
        filesPath = "/var/cache/bedrock/files";
    }

    if (!directoryExists(filesPath)) {
        if (!makeDirectory(filesPath)) {
            string errMsg = "Could not create files directory: ";
            errMsg += filesPath;
            STHROW(errMsg);
        }
    }

    // Save this in a class constant, to enable us to access it safely in an
    // unsynchronized manner from other threads.
    *((string*)&_filesPath) = filesPath;
}

// ==========================================================================
void BedrockPlugin_Files::upgradeDatabase(SQLite& db) {
    // Create or verify the files table
    bool created;
    while (
        !db.verifyTable(
            "files",
            "CREATE TABLE files ( "
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT, "
                "path TEXT, "
                "type TEXT, "
                "size INTEGER ) ",
           created
        )
    ) {
        // Drop and rebuild the index
        SASSERT(db.write("DROP index filesNamePath;"));

        // Drop and rebuild the table
        SASSERT(db.write("DROP TABLE files;"));
    }

    if (created) {
        db.write("create index filesNamePath on files (name, path);");
    }
}

// ==========================================================================
bool BedrockPlugin_Files::peekCommand(SQLite& db, BedrockCommand& command) {
    // Pull out some helpful variables
    SData& request = command.request;
    SData& response = command.response;

    // ----------------------------------------------------------------------
    if (SIEquals(request.getVerb(), "ReadFile")) {
        // - ReadFile( id )
        //
        //     Gets a file by id.
        //
        //     Parameters:
        //     - id - the unique id of the file
        //
        //     Returns:
        //     - 200 - OK
        //         . name - name of the file
        //         . path - path of the file
        //         . type - content type of the file
        //         . size - lenght in bytes of the file
        //         . content - content of the file in the body
        //     - 404 - No file found
        //
        // - ReadFile( name, path )
        //
        //     Gets a file by name and path.
        //
        //     Parameters:
        //     - name - the name of the file
        //     - path - path of the file
        //
        //     Returns:
        //     - 200 - OK
        //         . id - unique id of the file
        //         . type - content type of the file
        //         . size - lenght in bytes of the file
        //         . content - content of the file in the body
        //     - 404 - No file found
        //
        SQResult result;
        bool byID= false;

        if (!request["id"].empty()) {
            uint64_t id = SToUInt64(request["id"]);
            if (
                !db.read(
                    "SELECT path, name, type, size "
                        "FROM files "
                        "WHERE id=" + SQ(id) + " "
                        "LIMIT 1;",
                    result
                )
            ) {
                STHROW("502 Query by id failed");
            } else {
                byID = true;
            }
        }

        if (!byID && !request["name"].empty() && !request["path"].empty()) {
            if (
                !db.read(
                    "SELECT id, type, size "
                        "FROM files "
                        "WHERE name=" + SQ(request["name"]) + " "
                        "and "
                        "path=" + SQ(trim(request["path"], "/")) + " "
                        "LIMIT 1;",
                    result
                )
            ) {
                STHROW("502 Query by name and path failed");
            }
        }

        // If we didn't get any results, respond failure
        if (result.empty()) {
            // No results
            STHROW("404 No match found");
        } else {
            // Return that item
            if (byID) {
                SASSERT(result[0].size() == 4);
                response["path"] = result[0][0];
                response["name"] = result[0][1];
                response["type"] = result[0][2];
                response["size"] = result[0][3];
                response.content = SFileLoad(
                    _filesPath + "/" + result[0][0] + "/" + result[0][1]
                );
            } else {
                SASSERT(result[0].size() == 3);
                response["id"] = result[0][0];
                response["type"] = result[0][1];
                response["size"] = result[0][2];
                response.content = SFileLoad(
                    _filesPath + "/" + request["path"] + "/" + request["name"]
                );
            }

            return true;
        }
    }

    // Didn't recognize this command
    return false;
}

// ==========================================================================
bool BedrockPlugin_Files::processCommand(SQLite& db, BedrockCommand& command) {
    // Pull out some helpful variables
    SData& request = command.request;
    SData& response = command.response;

    // ----------------------------------------------------------------------
    if (SIEquals(request.getVerb(), "WriteFile")) {
        // - WriteFile( path, name, type )
        //
        //     Adds a new file.
        //
        //     Parameters:
        //     - path - path of file
        //     - name - name of the file
        //     - type - content type
        //
        verifyAttributeSize(request, "path", 1, MAX_SIZE_SMALL);
        verifyAttributeSize(request, "name", 1, MAX_SIZE_SMALL);
        verifyAttributeSize(request, "type", 1, MAX_SIZE_SMALL);

        if (!request.content.empty()) {
            // Value is provided via the body -- make sure it's not too long
            if (request.content.size() > 64 * 1024 * 1024) {
                STHROW("402 Content too large, 64MB max");
            }
        } else {
            // No value provided
            STHROW("402 Missing content body");
        }

        // Check if the file already exists
        SQResult result;
        if (
            !db.read(
                "SELECT id "
                    "FROM files "
                    "WHERE name=" + SQ(request["name"]) + " "
                    "and "
                    "path=" + SQ(trim(request["path"], "/")) + " "
                    "LIMIT 1;",
                result
            )
        ) {
            STHROW("502 Query by name and path failed");
        }

        // Insert or update the file
        const string& path = trim(request["path"], "/");
        const string& name = request["name"];
        const string& type = request["type"];
        const string& filePath = _filesPath
            + "/" + request["path"]
            + "/" + request["name"]
        ;

        if (result.empty()) {
            // Insert the new entry
            if (
                !db.write(
                    "INSERT INTO files ( path, name, type, size ) "
                        "VALUES( "
                            + SQ(path) + ", "
                            + SQ(name) + ", "
                            + SQ(type) + ", "
                            + SQ(request.content.size())
                            + " );"
                )
            ) {
                STHROW("502 Query failed (inserting)");
            } else {
                makeDirectory(_filesPath + "/" + request["path"]);
                if (!SFileSave(filePath, request.content))
                    STHROW("502 Failed to add new file");

                response["id"] = SToStr(db.getLastInsertRowID());
            }
        } else {
            // Update an existing entry
            SASSERT(result[0].size() == 1);
            const uint64_t id = SToUInt64(result[0][0]);
            if (
                !db.write(
                    "UPDATE files set "
                        "path=" + SQ(path) + ", "
                        "name=" + SQ(name) + ", "
                        "type=" + SQ(type) + ", "
                        "size=" + SQ(request.content.size()) + " "
                        "where id=" + SQ(id) + ";"
                )
            ) {
                STHROW("502 Query failed (updating)");
            } else {
                makeDirectory(_filesPath + "/" + request["path"]);
                if (!SFileSave(filePath, request.content))
                    STHROW("502 Failed to update file");

                response["id"] = SToStr(id);
            }
        }

        return true; // Successfully processed
    }

    // ----------------------------------------------------------------------
    else if (SIEquals(request.getVerb(), "DeleteFile")) {
        // - DeleteFile( id )
        //
        //     Deletes a file by id.
        //
        //     Parameters:
        //     - id     - id of the file to delete
        //
        // - DeleteFile( name, path )
        //
        //     Deletes a file by name and path.
        //
        //     Parameters:
        //     - name   - name of the file
        //     - path   - path to file
        //

        // Delete file by id.
        if (!request["id"].empty()) {
            uint64_t id = SToUInt64(request["id"]);
            SQResult result;
            if (
                !db.read(
                    "SELECT path, name "
                        "FROM files "
                        "WHERE id=" + SQ(id) + " "
                        "LIMIT 1;",
                    result
                )
            ) {
                STHROW("502 Query id failed");
            }

            if (!result.empty()) {
                if (!db.write("DELETE FROM files WHERE id=" + SQ(id) + ";")) {
                    STHROW("502 Query failed (by id)");
                } else {
                    deleteFile(_filesPath + "/" + result[0][0] + "/" + result[0][1]);
                }
                return true;
            } else {
                STHROW("404 No match found");
            }
        } else if (!request["name"].empty() && !request["path"].empty()) {
            if (
                !db.write(
                    "DELETE FROM files WHERE "
                        "path=" + SQ(trim(request["path"], "/")) + " "
                        "and "
                        "name=" + SQ(request["name"]) + ";"
                )
            ) {
                STHROW("502 Query failed (delete)");
            } else {
                deleteFile(
                    _filesPath + "/" + trim(request["path"], "/") + "/" + request["name"]
                );
            }
            return true;
        }

        STHROW("402 Missing File ID or Name and Path");
    }

    // Didn't recognize this command
    return false;
}

// ==========================================================================
bool BedrockPlugin_Files::deleteFile(const string& filePath) {
    const int result = unlink(filePath.c_str());
    if (result != 0) {
        SWARN("Failed deleting file '" << filePath << " code: " << result);
        return false;
    }

    // now we will try to remove empty directories
    vector<string> fileParts = split(filePath, "/");
    fileParts.pop_back(); // remove file name

    string path = "";
    for(size_t i=0; i<fileParts.size(); i++) {
        path += "/" + fileParts[i];
    }

    if (trim(path, "/") != trim(_filesPath, "/")) {
        do {
            if(isDirectoryEmpty(path)) {
                const int result = rmdir(path.c_str());
                if (result != 0) {
                    SWARN("Failed deleting path '" << path << " code: " << result);
                    return false;
                }
            } else {
                break;
            }
            fileParts.pop_back(); // remove another directory
            string newPath = "";
            for(size_t i=0; i<fileParts.size(); i++) {
                newPath += "/" + fileParts[i];
            }
            path = newPath;
        } while (trim(path, "/") != trim(_filesPath, "/"));
    }

    return true;
}

// ==========================================================================
bool BedrockPlugin_Files::makeDirectory(const string& path) {
    vector<string> path_parts = split(path, "/");
    string current_path = "";

    for(size_t i=0; i<path_parts.size(); i++) {
        current_path += "/" + path_parts[i];
        if(!directoryExists(current_path)) {
            const int dir_err = mkdir(
                current_path.c_str(), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH
            );

            if (-1 == dir_err) {
                return false;
            }
        }
    }

    return true;
}

// ==========================================================================
bool BedrockPlugin_Files::directoryExists(const string& path) {
    // Return true if it exists and is a directory
    struct stat out;

    if (stat(path.c_str(), &out) != 0) {
        return false;
    }

    return (out.st_mode & S_IFDIR) != 0;
}

// ==========================================================================
bool BedrockPlugin_Files::isDirectoryEmpty(const string& dirname) {
    int n = 0;
    struct dirent *d;
    DIR *dir = opendir(dirname.c_str());

    if (dir == NULL) //Not a directory or doesn't exist
        return true;

    while ((d = readdir(dir)) != NULL) {
        if(++n > 2)
            break;
    }

    closedir(dir);

    if (n <= 2) //Directory Empty
        return true;

    return false;
}

// ==========================================================================
string BedrockPlugin_Files::trim(const string& str, const string& chars) {
    string newString = str;

    // trim characters
    size_t endpos = newString.find_last_not_of(chars);
    size_t startpos = newString.find_first_not_of(chars);

    if ( string::npos != endpos ) {
        newString = newString.substr( 0, endpos+1 );
        newString = newString.substr( startpos );
    } else {
        newString.erase(
            std::remove(
                std::begin(newString),
                std::end(newString),
                ' '
            ),
            std::end(newString)
        );
    }

    return newString;
}

// ==========================================================================
vector<string> BedrockPlugin_Files::split(const string& str, const string& delim) {
    vector<string> tokens;
    size_t prev = 0, pos = 0;

    do {
        pos = str.find(delim, prev);
        if (pos == string::npos) pos = str.length();
        string token = str.substr(prev, pos-prev);
        if (!token.empty()) tokens.push_back(token);
        prev = pos + delim.length();
    } while (pos < str.length() && prev < str.length());

    return tokens;
}