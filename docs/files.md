---
layout: default
---

# Bedrock::Files -- Simple, distributed, replicated filesystem
Bedrock::Files is a plugin to the [Bedrock data foundation](../README.md) 
providing a distributed filesystem that can be used to store images and any 
other kind of files.

Commands include:

 * **ReadFile( id )** - Get the file content by id
   * *id* - unique id of the file
   * Returns:
     * *path* - eg: some/path
     * *name* - eg: mydocument.txt
     * *type* - content mime type, eg: text/plain
     * *size* - size of the file in bytes
     * *content* - content of file in the body of the response

 * **ReadFile( path, name )** - Get the file content corresponding to a path and name
   * *path* - path where the file resides
   * *name* - name of the file
   * Returns:
     * *id* - unique id of the file
     * *type* - content mime type, eg: text/plain
     * *size* - size of the file in bytes
     * *content* - content of file in the body of the response

 * **WriteFile( path, name, type, content )** - Adds a new file or overwrites an existing one.
   * *path* - location to store file relative to -files.path eg: documents/markdown
   * *name* - A name for the file eg: readme.md
   * *type* - A valid mimtype eg: text/markdown
   * *content* - content of file which is read from the content body (64MB max)

 * **DeleteFile( id )** - Deletes a file by id
   * *id* - unique id of the file

 * **DeleteFile( path, name )** - Delete file by path and name
   * *path* - path where the file resides
   * *name* - name of the file

## Sample Session
This session shows how to add or overwrite a file.

    WriteFile
    path: documents/markdown
    name: README.md
    type: text/markdown
    content-length: 13

    # Hello World

    200 OK
    id: 1

This can be retreived with ReadFile. The content of file is returned in the body of the response:

    ReadFile
    id: 1

    200 OK
    path: documents/markdown
    name: README.md
    type: text/markdown
    size: 13
    Content-Length: 13

    # Hello World

Also you can retrieve a file by name and path:

    ReadFile
    path: documents/markdown
    name: README.md

    200 OK
    id: 1
    type: text/markdown
    size: 13
    Content-Length: 13

    # Hello World

Finally, you can delete files easily:

    DeleteFile
    path: documents/markdown
    name: README.md

    200 OK
    Content-Length: 0

Or by id:

    DeleteFile
    id: 1

    200 OK
    Content-Length: 0

