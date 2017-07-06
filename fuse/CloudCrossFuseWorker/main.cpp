﻿#include <iostream>
#include <fstream>

#include <stdio.h>
#include <wchar.h>

#define FUSE_USE_VERSION 26
#define _FILE_OFFSET_BITS  64
#define DAEMON_MSG_END "\n<---CCFD_MESSAGE_END-->"

#define WAIT_FOR_FIRST_DATA 7
#define WAIT_FOR_NEXT_DATA 6

#include <fuse.h>
#include <string.h>
#include <errno.h>
#include <map>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/un.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <pthread.h>

#include "json.hpp"




using namespace std;
using json = nlohmann::json;

#include "ccfw.h"
#include "incominglistener.cpp"
#include "fswatcher.cpp"

static const char *filepath = "/file";
static const char *filename = "file";
static const char *filecontent = "I'm the content of the only file available there\n";

// forward declarations
void log(string mes);
string sendCommand_sync(json command);
void sendCommand(json command);


// ========= GLOBALS ==========

static  fuse_operations fuse_op;

static int sock_descr;

//std::map<std::string,MSFSObject> fileList;

static string tempDirName;

static string workSocket;
static string workProvider;
static string workPath;
static string mountPoint;

pthread_t listenerThread;
lst_param listenerThreadParams;

pthread_t fsWatherThread;
fswatcher_param fsWatherThreadParams;

// ----------------------------------------------------------------

int readFileContent(char* path,char* buf,int size, int offset){



}

// ----------------------------------------------------------------

int f_mknod(const string &path){

    int r= system(string("touch \""+path+"\"").c_str());
    if(r == 0){
        return r;
    }
    else{
        return -1;
    }
}


int f_mkdir(const string &path){

    int r= system(string("mkdir -p \""+path+"\"").c_str());
    if(r == 0){
        return r;
    }
    else{
        return -1;
    }
}

// ----------------------------------------------------------------

string getTempName() {
    static const char alphanum[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    string rnd;
    srand (time(NULL));

    for (int i = 0; i < 7; ++i) {
        rnd+= alphanum[rand() % (sizeof(alphanum) - 1)];
    }

    return rnd+"-temp";
}




// ----------------------------------------------------------------

MSFSObject convertJsonToFS_remote(const json &jo){

    MSFSObject obj;

    obj.state               = jo["state"];
    obj.path                = jo["path"];
    obj.fileName            = jo["fileName"];
    obj.refCount            = -1;

    //obj.remote.data         = jo["remote"]["data"];
    obj.remote.exist        = true;
    obj.remote.fileSize     = jo["remote"]["fileSize"];
    obj.remote.md5Hash      = jo["remote"]["md5Hash"];
    obj.remote.modifiedDate = jo["remote"]["modifiedDate"];
    obj.remote.objectType   = jo["remote"]["objectType"];

    return obj;
}

// ----------------------------------------------------------------

bool findObjectInFilelist(const string &path, MSFSObject* obj ){

//    map<string, MSFSObject>::iterator i= fileList.find(string(path)); //does try find in local filelist

//    if( i != fileList.end() ){// if it was finded in local filelist

//        obj = &i->second;

//        return true;
//    }
//    else{

        json comm;
        comm["command"] = "find_object";
        comm["params"]["socket"] = workSocket;
        comm["params"]["provider"] = workProvider;
        comm["params"]["path"] = workPath;
        comm["params"]["mount"] = mountPoint;
        comm["params"]["objectPath"] = path;

        json jsonFileList = json::parse( sendCommand_sync(comm) );

        if(jsonFileList["code"] == 0){

            if(obj == 0){
                return true;
            }

            json jo = jsonFileList["data"];

            obj->state               = jo["state"];
            obj->path                = jo["path"];
            obj->fileName            = jo["fileName"];
            obj->refCount            = -1;

            //obj.remote.data         = jo["remote"]["data"];
            obj->remote.exist        = true;
            obj->remote.fileSize     = jo["remote"]["fileSize"];
            obj->remote.md5Hash      = jo["remote"]["md5Hash"];
            obj->remote.modifiedDate = jo["remote"]["modifiedDate"];
            obj->remote.objectType   = jo["remote"]["objectType"];

            return true;
        }
        else{


            return false;
        }

//    }




}

// ----------------------------------------------------------------

map<string, MSFSObject> filterListByPath(/*map<string, MSFSObject> src,*/ string path){

    log(" filterListByPath sent "+path);
    json comm;
    comm["command"] = "get_dir_list";
    comm["params"]["socket"] = workSocket;
    comm["params"]["provider"] = workProvider;
    comm["params"]["path"] = workPath;
    comm["params"]["mount"] = mountPoint;
    comm["params"]["dirPath"] = path;

    string reply = sendCommand_sync(comm) ;
    json jsonFileList = json::parse( reply );

    log(" filterListByPath receive "+reply);

    map<string, MSFSObject> fl;

        for(json::iterator i = jsonFileList.begin(); i != jsonFileList.end(); i++){

            MSFSObject obj = convertJsonToFS_remote(i.value());
//            json jo=i.value();
//            obj.state               = jo["state"];
//            obj.path                = jo["path"];
//            obj.fileName            = jo["fileName"];
//            obj.refCount            = -1;

//            //obj.remote.data         = jo["remote"]["data"];
//            obj.remote.exist        = true;
//            obj.remote.fileSize     = jo["remote"]["fileSize"];
//            obj.remote.md5Hash      = jo["remote"]["md5Hash"];
//            obj.remote.modifiedDate = jo["remote"]["modifiedDate"];
//            obj.remote.objectType   = jo["remote"]["objectType"];


            fl.insert(std::make_pair(i.key(),obj));


        }

    return fl;

//    map<string, MSFSObject>::iterator i=src.begin();
//    map<string,MSFSObject> out;

//    for(;i != src.end(); i++){

//        MSFSObject o=i->second;
//        if(o.path == path){
//            out.insert(std::make_pair(i->first,i->second));
//        }
//    }

//    return out;

}


// ----------------------------------------------------------------

static int fgetattr_callback(const char *path, struct stat *stbuf, struct fuse_file_info * fi) {

//log("STUB fgetattr");
return -ENOENT;
}

// ----------------------------------------------------------------

static int getattr_callback(const char *path, struct stat *stbuf) {

    if (strcmp(path, "/") == 0){
        log("CALLBACK getattr for /");

              stbuf->st_mode = S_IFDIR | 0755;
              stbuf->st_uid=getuid();
              stbuf->st_gid=getgid();
              stbuf->st_nlink = 1;
              return 0;
    }
    else{

        string fname = string("/tmp/")+tempDirName+string(path);
        //string findname=string(path)+"/";
        log("CALLBACK getattr "+fname);

        struct stat buffer;
        if((stat (fname.c_str(), &buffer) != 0) ){//file not cached
        log("CALLBACK getattr:  file not cached");

            //map<string, MSFSObject>::iterator i= fileList.find(string(path));

         MSFSObject o;

                if(findObjectInFilelist(path,&o)){// file or folder exists on remote

                //if(i != fileList.end()){// file or folder exists on remote

                    log("CALLBACK getattr:  file or folder exists on remote");

                    //MSFSObject o= i->second;

                    memset(stbuf,0,sizeof(struct stat));

                    if(o.remote.objectType == MSRemoteFSObject::Type::folder){

                        stbuf->st_mode = S_IFDIR | 0755;
                        stbuf->st_nlink = 2;
                        //stbuf->st_size = 4096;
                    }
                    else{

                        stbuf->st_mode = S_IFREG | 0777;
                        stbuf->st_nlink = 1;
                        stbuf->st_size = o.remote.fileSize;
                    }

                    stbuf->st_uid=getuid();
                    stbuf->st_gid=getgid();
                    stbuf->st_mtime=o.remote.modifiedDate/1000;

                    return 0;
                }
                else{ // this is new file or folder
                    log("CALLBACK getattr:  this is new file or folder");

//                    stbuf->st_mode = S_IFREG | 0777;
//                    stbuf->st_nlink = 1;
//                    stbuf->st_size = 0;
//                    stbuf->st_uid=1000;
//                    stbuf->st_gid=1000;
//                    stbuf->st_mtime=time(NULL);

                    //stat (fname.c_str(), stbuf);

                   // log("CALLBACK getattr NEW FILE "+to_string(stbuf->st_mode));
                   //memset(stbuf, 0, sizeof(stbuf));
//                    stbuf->st_mode = S_IFREG | 0777;
//                    stbuf->st_uid = getuid();
//                    stbuf->st_gid = getgid();
//                    stbuf->st_atime = stbuf->st_mtime = time(NULL);

                    return -ENOENT;
                   // return 0;
                }

              return 0;
        }
        else{// file cached

            log("CALLBACK getattr:  file cached");
            int res;
            res = stat(fname.c_str(), stbuf);


            if (res == -1){
                    return -errno;
            }
            //log("CALLBACK getattr CACHED FILE SIZE IS "+to_string(stbuf->st_size));
            return 0;

        }


    }

    return -ENOENT;
}

// ----------------------------------------------------------------


static int readdir_callback(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi){

    (void) offset;
    (void) fi;
    log("CALLBACK readdir "+string(path));


//            filler(buf, ".", NULL, 0);
//            filler(buf, "..", NULL, 0);

     string m_path;

     if(strcmp(path, "/") == 0){

         m_path=string(path);
     }
     else{
        m_path=string(path)+"/";
     }

    map<string, MSFSObject> dl= filterListByPath( /*fileList,*/ string(m_path));

    if( ! dl.empty() ){

        map<string, MSFSObject>::iterator i=dl.begin();

        for(;i != dl.end();i++){

            struct stat st;
            memset(&st, 0, sizeof(struct stat));

            if(i->second.remote.objectType == MSRemoteFSObject::Type::folder){

                st.st_mode = S_IFDIR | 0755;
                st.st_nlink = 2;
            }
            else{

                st.st_mode = S_IFREG | 0777;
                st.st_nlink = 1;
                st.st_size = i->second.remote.fileSize;
            }

            st.st_uid=getuid();
            st.st_gid=getgid();
            st.st_mtime=i->second.remote.modifiedDate/1000;


            filler(buf,i->second.fileName.c_str(),&st,0);

        }


        return 0;
    }
    else{
        struct stat st;
        memset(&st, 0, sizeof(struct stat));
        st.st_mode = S_IFDIR | 0755;
        st.st_nlink = 2;
        st.st_uid=getuid();
        st.st_gid=getgid();
        filler(buf,".",&st,0);

        memset(&st, 0, sizeof(struct stat));
        st.st_mode = S_IFDIR | 0755;
        st.st_nlink = 2;
        st.st_uid=getuid();
        st.st_gid=getgid();
        filler(buf,"..",&st,0);

//        filler(buf, ".", NULL, 0);
//        filler(buf, "..", NULL, 0);
        return 0;
    }

    //return -1;


}

static int open_callback(const char *path, struct fuse_file_info *fi) {

    //map<string, MSFSObject>::iterator i= fileList.find(string(path));

    //MSFSObject o;

    if(findObjectInFilelist(path,nullptr)){

    //if(i != fileList.end()){

        string fname=string("/tmp/")+tempDirName+string(path);

        struct stat buffer;
        if(stat (fname.c_str(), &buffer) == 0){//file exists

            //log("CALLBACK open - File Exists "+string(path)+" flags = "+to_string(fi->flags));

            //i->second.refCount++;

            fi->fh = open (fname.c_str() ,fi->flags);
            if(fi->fh == -1){
                //log("CALLBACK open EXISTS - open for writing error");
            }

        }
        else{// file is missing

           // log("CALLBACK open - File Missing");

            string ptf=fname.substr(0,fname.find_last_of("/"));

            system(string(string("mkdir -p \"")+ptf+string("\"")).c_str());

            // create zero-size file
            fstream fs;
            fs.open(fname.c_str(), ios::out);
            fs.close();

            fi->fh = open (fname.c_str() ,fi->flags);
            if(fi->fh == -1){
               // log("CALLBACK open MISSING - open for writing error");
            }

            json comm;
            comm["command"]="get_content";
            comm["params"]["socket"]=workSocket;
            comm["params"]["provider"]=workProvider;
            comm["params"]["path"]=workPath;
            comm["params"]["cachePath"]=fname;
            comm["params"]["filePath"]=string(path);

            sendCommand(comm) ;

            //i->second.refCount=0;
        }

        //log("open callback ended for "+string(path)+" ; ref count is "+to_string(i->second.refCount));
        return 0;
    }

  return -1;
}


// ----------------------------------------------------------------

static int read_callback(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi){

//    log("read "+to_string(size)+" "+to_string(offset));

    MSFSObject o;

    findObjectInFilelist(path,&o);

    //map<string, MSFSObject>::iterator i= fileList.find(string(path));

    int fullSize = o.remote.fileSize;

    string fname = string("/tmp/")+tempDirName+string(path);

    struct stat buffer;
    if(stat (fname.c_str(), &buffer) == 0){//file exists in cache

//        // get real size of file
//        FILE* r_f=fopen(fname.c_str(),"rb");
//        fseek(r_f,0,SEEK_END);
//        buffer.st_size = ftell(r_f);
//        fclose(r_f);


        if(buffer.st_size == 0){// file don't cached yet

            if(o.localCreated){

                //log("CALLBACK read - file LOCAL CREATED and fully cached "+string(path));

                FILE* f = fopen(fname.c_str(),"rb");
                fseek(f,offset,SEEK_SET);
                size_t cnt = fread(buf,1,size,f);
                fclose(f);

                return cnt;
            }
            else{

                //log("CALLBACK read - file exists but don't cached "+string(path));

                for(int i=0;i < WAIT_FOR_FIRST_DATA;i++){
                    sleep(1);
                    stat (fname.c_str(), &buffer);
                    if( (buffer.st_size == fullSize) || (buffer.st_size == size) ){

                        return read_callback(path,buf,size,offset,fi);
                    }
                }

                return 0;
            }

        }
        else{// fully or partialy cached

            if(fullSize == buffer.st_size){// fully cached


//                if(offset > buffer.st_size){
//                    log("CALLBACK read - (offset > buffer.st_size) "+string(path));
//                    return 0;
//                }

//                if(offset + size > buffer.st_size){

//                    log("CALLBACK read - (offset + size > buffer.st_size) "+string(path));

//                    FILE* f = fopen(fname.c_str(),"rb");
//                    fseek(f,offset,SEEK_SET);
//                    //size_t cnt = fread(buf,1,(buffer.st_size - offset),f);
//                    size_t cnt = fread(buf,1,(size),f);
//                    fclose(f);
//                    return (int)size;//(buffer.st_size - offset);
//                    //return 0;
//                }


//                log("CALLBACK read - NORM "+string(path));
//                FILE* f = fopen(fname.c_str(),"rb");
//                fseek(f,offset,SEEK_SET);
//                size_t cnt = fread(buf,1,size,f);
//                fclose(f);

//                if(cnt != size){
//                    return (int)cnt;
//                }
//                else{
//                    return (int)size;
//                }


                    //log("CALLBACK read - NEW "+to_string(fullSize)+" ("+to_string(buffer.st_size)+") "+to_string(offset)+" "+to_string(size));

                    ifstream data;
                    data.open(fname.c_str(),   ios::in | ios::binary);//ios::out |ios::trunc |

                    data.seekg(static_cast<streamoff>(offset));
                    data.read(buf, static_cast<streamoff>(size));
                    if (data.eof()){ // read may fail if we reach eof

                        data.clear(); // clear possible failbit
                    }

                   // log("CALLBACK read - NEW READED "+to_string(static_cast<int>(data.gcount())));

                    return static_cast<int>(data.gcount());




//                log("CALLBACK read - file exists and fully cached "+string(path));

//                FILE* f = fopen(fname.c_str(),"rb");
//                fseek(f,offset,SEEK_SET);
//                size_t cnt = fread(buf,1,size,f);
//                //fclose(f);

//                if(cnt < 0){
//                    fclose(f);
//                    return 0;
//                }

//                if(cnt != size){
//                    int y;
//                    if(cnt > size){
//                        y = cnt - size;
//                        log("CALLBACK read - ERROR OCCURED (CNT is greater) SIZE MISSMATCH IS  "+to_string(y));
//                        fclose(f);
//                        return size;
//                    }
//                    else{

//                        y = size - cnt;
//                        log("CALLBACK read - ERROR OCCURED (SIZE is greater) SIZE MISSMATCH IS  "+to_string(y));

//                        fclose(f);
//                        return cnt;

//                    }
//                    //log("CALLBACK read - ERROR OCCURED SIZE MISSMATCH IS  "+to_string(y));

//                    fclose(f);
//                }

//                log("CALLBACK read - WAS READED  "+to_string(cnt));
//                return cnt;
            }
            else{// partialy cached

               // log("CALLBACK read - file exists and partially cached "+string(path));

                int endPosition=size+offset-1;

                if(endPosition > fullSize){
                    endPosition = fullSize;
                }

                for(int i=0;i < WAIT_FOR_NEXT_DATA;i++){
                    sleep(1);
                    stat (fname.c_str(), &buffer);
                    if( (endPosition <= buffer.st_size) ){

                        FILE* f = fopen(fname.c_str(),"rb");
                        fseek(f,offset,SEEK_SET);
                        size_t cnt = fread(buf,1,size,f);
                        fclose(f);

                        return cnt;

                    }

                    return 0;
                }



                if(offset < buffer.st_size){// i can read at least few bytes

                }
                else{// required file range don't cached. need wait

                }

            }

        }

    }
    else{

        //log("CALLBACK read - file DON'T exist "+string(path));
        return open_callback(path,fi);
    }


return -ENOENT;
}

// ----------------------------------------------------------------

static int write_callback(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi){

   // log("CALLBACK write");
    string fname = string("/tmp/")+tempDirName+string(path);

//    ofstream of;
//    of.open(fname.c_str());
//    of.seekp(offset);
//    of.write(buf,size);
//    of.close();

////    FILE* f = fopen(fname.c_str(),"wb+");
////    fseek(f,offset,SEEK_SET);
////    size_t cnt = fread(buf,1,size,f);
////    fclose(f);

//    return size;

    int fd;
    int res;
    (void) fi;
    fd = open(fname.c_str(), O_WRONLY);
    if (fd == -1)
            return -errno;
    res = pwrite(fd, buf, size, offset);
    if (res == -1){
        //log("write callback ERROR");
        close(fd);

        return -errno;
    }
    close(fd);

    struct stat buffer;
    stat (fname.c_str(), &buffer);

    //map<string, MSFSObject>::iterator i= fileList.find(string(path));

    MSFSObject o;

    findObjectInFilelist(path,&o);

    o.remote.fileSize=buffer.st_size;

   // log("truncated to "+to_string(buffer.st_size));

    //log("write callback OK");

    json comm;
    comm["command"]="need_update";
    comm["params"]["socket"]=workSocket;
    comm["params"]["provider"]=workProvider;
    comm["params"]["path"]=workPath;
    comm["params"]["cachePath"]=fname;
    comm["params"]["filePath"]=string(path);

    sendCommand(comm) ;

    return res;

}


static int ftruncate_callback(const char *path, off_t size,struct fuse_file_info *fi)
{
    (void)fi;
    //log("CALLBACK ftruncate");
    string fname = string("/tmp/")+tempDirName+string(path);


        int res;
        res = truncate(fname.c_str(), size);
        if (res == -1){
                return -errno;
        }


        return 0;
}



static int truncate_callback(const char *path, off_t size)
{

    //log("CALLBACK truncate");
    string fname = string("/tmp/")+tempDirName+string(path);


        int res;
        res = truncate(fname.c_str(), size);
        if (res == -1){
                return -errno;
        }


        return 0;
}


static int xmp_readlink(const char *path, char *buf, size_t size){

    //log("STUB: readlink");
    return 0;
}


static int xmp_mknod(const char *path, mode_t mode, dev_t rdev){
//log("CALLBACK mknod");

    string fname = string("/tmp/")+tempDirName+string(path);
    string insname=string(path);

    log("CALLBACK mknod begin"+fname);

    int res = f_mknod(fname.c_str());

    if(res == -1){


        string td = string("/tmp/")+tempDirName;
        string ptf = fname.substr(0,fname.find_last_of("/"));
        //ptf = ptf.substr(td.length());

//        if(ptf.length() == 0){
//            ptf = "/";
//        }

        log("CALLBACK mknod failed. Try create dir"+ptf);

        res = f_mkdir(ptf.c_str());
        if(res == -1){
            log("CALLBACK mknod create dir failed. ");
            return -errno;
        }
        else{
            res = f_mknod(fname.c_str());
            if(res == -1){
                log("CALLBACK mknod  failed again.");
                return -errno;
            }
        }


    }



    log("CALLBACK mknod OK. ");

        MSFSObject obj;

        string td = string("/tmp/")+tempDirName;
        string ptf = fname.substr(0,fname.find_last_of("/"));
        ptf = ptf.substr(td.length());

        if(ptf.length() == 0){
            ptf = "/";
        }
        else{
            ptf = ptf
                    +"/";
        }

        json comm1;
        comm1["command"]="add_object";
        comm1["params"]["socket"]=workSocket;
        comm1["params"]["provider"]=workProvider;
        comm1["params"]["path"]=workPath;
        comm1["params"]["cachePath"]=fname;
        comm1["params"]["filePath"]=fname;

        sendCommand(comm1) ;

//        string fnm=fname.substr(fname.find_last_of("/")+1);

////        obj.state               = jo["state"];
//        obj.path                = ptf;
//        obj.fileName            = fnm;
//        obj.refCount            = 1;
//        obj.remote.exist        = true;
//        obj.remote.fileSize     = 0;
//        obj.remote.modifiedDate = time(0);
//        obj.remote.objectType   = MSRemoteFSObject::Type::file;
//        obj.localCreated        = true;

//        fileList.insert(std::make_pair(string(insname),obj));

        //log("CALLBACK mknod NEW CREATED "+ ptf+string(" - ")+fnm);

        json comm;
        comm["command"]="need_update";
        comm["params"]["socket"]=workSocket;
        comm["params"]["provider"]=workProvider;
        comm["params"]["path"]=workPath;
        comm["params"]["cachePath"]=fname;
        comm["params"]["filePath"]=string(path);

        sendCommand(comm) ;

        return 0;


}


static int xmp_mkdir(const char *path, mode_t mode){

   // log("CALLBACK mkdir");

    string fname = string("/tmp/")+tempDirName+string(path);
    string insname=string(path);

        int res = f_mkdir(fname.c_str());
        if(res == -1){
            return -errno;
        }
        else{

            MSFSObject obj;

            string td = string("/tmp/")+tempDirName;
            string ptf = fname.substr(0,fname.find_last_of("/"));
            ptf = ptf.substr(td.length());

            if(ptf.length() == 0){
                ptf = "/";
            }
            else{
                ptf = ptf +"/";
            }


            json comm1;
            comm1["command"]="add_object";
            comm1["params"]["socket"]=workSocket;
            comm1["params"]["provider"]=workProvider;
            comm1["params"]["path"]=workPath;
            comm1["params"]["cachePath"]=fname;
            comm1["params"]["filePath"]=fname;

            sendCommand(comm1) ;

//            string fnm=fname.substr(fname.find_last_of("/")+1);

//    //        obj.state               = jo["state"];
//            obj.path                = ptf;
//            obj.fileName            = fnm;
//            obj.refCount            = 1;
//            obj.remote.exist        = true;
//            obj.remote.fileSize     = 0;
//            obj.remote.modifiedDate = time(0);
//            obj.remote.objectType   = MSRemoteFSObject::Type::folder;

//            fileList.insert(std::make_pair(string(insname),obj));

            //log("CALLBACK mkdir NEW CREATED "+ ptf+string(" - ")+fnm);


            json comm;
            comm["command"]="need_update";
            comm["params"]["socket"]=workSocket;
            comm["params"]["provider"]=workProvider;
            comm["params"]["path"]=workPath;
            comm["params"]["cachePath"]=fname;
            comm["params"]["filePath"]=string(path);

            sendCommand(comm) ;


            return 0;
        }
}


static int xmp_unlink(const char *path){

   // log("CALLBACK unlink");
    string fname = string("/tmp/")+tempDirName+string(path);
    //map<string, MSFSObject>::iterator i= fileList.find(string(path));

    int res = unlink(fname.c_str());



//        fileList.erase(i);

        json comm1;
        comm1["command"]="remove_object";
        comm1["params"]["socket"]=workSocket;
        comm1["params"]["provider"]=workProvider;
        comm1["params"]["path"]=workPath;
        comm1["params"]["cachePath"]=fname;
        comm1["params"]["filePath"]=path;

        sendCommand(comm1) ;


        json comm;
        comm["command"]="unlink";
        comm["params"]["socket"]=workSocket;
        comm["params"]["provider"]=workProvider;
        comm["params"]["path"]=workPath;
        comm["params"]["cachePath"]=fname;
        comm["params"]["filePath"]=string(path);

        sendCommand(comm) ;

        json comm2;
        comm2["command"]="need_update";
        comm2["params"]["socket"]=workSocket;
        comm2["params"]["provider"]=workProvider;
        comm2["params"]["path"]=workPath;
        comm2["params"]["cachePath"]=fname;
        comm2["params"]["filePath"]=string(path);

        sendCommand(comm2) ;


    return 0;




//    if( i != fileList.end()){

//        int res = unlink(fname.c_str());
//        if(res == -1){
//            return -errno;
//        }
//        else{
//            fileList.erase(i);

//            json comm;
//            comm["command"]="get_content";
//            comm["params"]["socket"]=workSocket;
//            comm["params"]["provider"]=workProvider;
//            comm["params"]["path"]=workPath;
//            comm["params"]["cachePath"]=fname;
//            comm["params"]["filePath"]=string(path);

//            sendCommand(comm) ;

//            return 0;
//        }
//    }

//    return -errno;
}


static int xmp_rmdir(const char *path){

    //log("CALLBACK: rmdir");
    string fname = string("/tmp/")+tempDirName+string(path);
    //map<string, MSFSObject>::iterator i= fileList.find(string(path));

    int res = rmdir(fname.c_str());






//        fileList.erase(i);

        json comm1;
        comm1["command"]="remove_object";
        comm1["params"]["socket"]=workSocket;
        comm1["params"]["provider"]=workProvider;
        comm1["params"]["path"]=workPath;
        comm1["params"]["cachePath"]=fname;
        comm1["params"]["filePath"]=path;

        sendCommand(comm1) ;

        json comm;
        comm["command"]="unlink";
        comm["params"]["socket"]=workSocket;
        comm["params"]["provider"]=workProvider;
        comm["params"]["path"]=workPath;
        comm["params"]["cachePath"]=fname;
        comm["params"]["filePath"]=string(path);

        sendCommand(comm) ;

        json comm2;
        comm2["command"]="need_update";
        comm2["params"]["socket"]=workSocket;
        comm2["params"]["provider"]=workProvider;
        comm2["params"]["path"]=workPath;
        comm2["params"]["cachePath"]=fname;
        comm2["params"]["filePath"]=string(path);

        sendCommand(comm2) ;

        return 0;

}


static int xmp_symlink(const char *from, const char *to){

    //log("STUB: symlink");
        return 0;
}


static int xmp_rename(const char *from, const char *to){

    log("CALLBACK rename");
    return -1;
//    stat buffer;

    string fname_from = string("/tmp/")+tempDirName+string(from);
    string fname_to = string("/tmp/")+tempDirName+string(to);



    int result= rename( fname_from.c_str() , fname_to.c_str() );
    if(result == 0){

        //map<string, MSFSObject>::iterator i= fileList.find(string(from));

        string td = string("/tmp/")+tempDirName;
        string ptf = fname_to.substr(0,fname_to.find_last_of("/"));
        ptf = ptf.substr(td.length());
        string fnm = fname_to.substr(fname_to.find_last_of("/")+1);

        if(ptf.length() == 0){
            ptf = "/";
        }


        json comm1;
        comm1["command"]="add_object";
        comm1["params"]["socket"]=workSocket;
        comm1["params"]["provider"]=workProvider;
        comm1["params"]["path"]=workPath;
        comm1["params"]["cachePath"]=fname_to;
        comm1["params"]["filePath"]=fname_to;

        sendCommand(comm1) ;


        json comm2;
        comm2["command"]="remove_object";
        comm2["params"]["socket"]=workSocket;
        comm2["params"]["provider"]=workProvider;
        comm2["params"]["path"]=workPath;
        comm2["params"]["cachePath"]=fname_from;
        comm2["params"]["filePath"]=from;

        sendCommand(comm2) ;


//        i->second.path = ptf;
//        i->second.fileName = fnm;

        json comm;
        comm["command"]="need_update";
        comm["params"]["socket"]=workSocket;
        comm["params"]["provider"]=workProvider;
        comm["params"]["path"]=workPath;
        comm["params"]["cachePath"]="";//fname;
        comm["params"]["filePath"]="";//string(path);

        sendCommand(comm) ;

        return 0;
    }
    else{
     return -1;
    }

}



static int xmp_link(const char *from, const char *to){

   // log("STUB: link");
        return 0;
}


static int xmp_chmod(const char *path, mode_t mode){

    //log("STUB: chmod");
        return 0;
}



static int xmp_chown(const char *path, uid_t uid, gid_t gid){

    //log("STUB: chown");
        return 0;
}



static int xmp_statfs(const char *path, struct statvfs *stbuf){

//    int res;
    string fname = string("/tmp/")+tempDirName+string(path);
//log("CALLBACK statfs "+fname);
//    res = statvfs(fname.c_str(), stbuf);
//    if (res == -1)
//            return -errno;
//    return 0;


//stbuf->f_fsid = {}; // ignored
//stbuf->f_bsize = 4096; // a guess!
//stbuf->f_blocks = 1000 * 256 ; // * 1024 * 1024 / f_bsize
//stbuf->f_bfree = stbuf->f_blocks - 2 * 256;
//stbuf->f_bavail = stbuf->f_bfree;
//stbuf->f_namemax = 256;
return 0;

}



static int xmp_access(const char *path, int mask){

    int res;
    string fname = string("/tmp/")+tempDirName+string(path);
//log("CALLBACK access "+fname);

    res = access(fname.c_str(), mask);
    if (res == -1)
            return -errno;
    return 0;
}


static int xmp_release(const char *path, struct fuse_file_info *fi)
{
        /* Just a stub.  This method is optional and can safely be left
           unimplemented */
        (void) path;
        (void) fi;

//log("CALLBACK release");

//        string fname = string("/tmp/")+tempDirName+string(path);

//        map<string, MSFSObject>::iterator i= fileList.find(string(path));

//        i->second.refCount--;
//        if(i->second.refCount < 0){
//            i->second.refCount = 0;
//        }

//        int rc=i->second.refCount;

//        log("release callback; ref count is "+to_string(rc));

        return 0;
}



static int xmp_fsync(const char *path, int isdatasync,struct fuse_file_info *fi){
        /* Just a stub.  This method is optional and can safely be left
           unimplemented */
//log("CALLBACK fsync");
        (void) path;
        (void) isdatasync;
        (void) fi;
        return 0;
}
// ----------------------------------------------------------------


static void destroy_callback(void* d){
    log("CALLBACK destroy");

    json comm;
    comm["command"] = "start_sync";
    comm["params"]["socket"] = workSocket;
    comm["params"]["provider"] = workProvider;
    comm["params"]["path"] = workPath;
    comm["params"]["mount"] = mountPoint;

    sendCommand_sync(comm);

    system(string("rm -rf /tmp/"+tempDirName).c_str());

    close(sock_descr);

    close(listenerThreadParams.incomingSocket);

    system(string("rm -rf /tmp/"+listenerThreadParams.socketName).c_str());

//    close(fsWatherThreadParams.wd);
//    inotify_rm_watch(fsWatherThreadParams.wd, fsWatherThreadParams.watcher);

    pthread_cancel(listenerThread);
//    pthread_cancel(fsWatherThread);

    // need send command for destroy socket and objects
}


// ----------------------------------------------------------------

bool connectToCommandServer(const string &socketName){

    struct sockaddr_un name;
    char buf[1000];
    int iMode=1;

    sock_descr=-1;

    memset(&name, '0', sizeof(name));

    name.sun_family = AF_UNIX;

    snprintf(name.sun_path, 200, "/tmp/%s", socketName.c_str());

    int s=socket(PF_UNIX,SOCK_STREAM,0);

    if(s== -1){
        return false;
    }

    int r=connect(s, (struct sockaddr *)&name, sizeof(struct sockaddr_un));

    if(r<0){
        perror ("connect");
        return false;
    }

    int sz= read(s,&buf[0],100);

    if(sz >0){
        string reply=&buf[0];

        if(reply.find("HELLO") != string::npos){

            //ioctl(s, FIONBIO, &iMode);
            fcntl(s,F_SETFD,O_NONBLOCK);
            sock_descr=s; // storing socket descriptor globaly
            return true;
        }
    }

    return false;
}

// ----------------------------------------------------------------

string readReplyFromCommandServer(){

    typedef std::vector<char> replyBuffer;
    replyBuffer data;
    int bytesRead ;

    log("ready for receiving data from daemon");

    do
    {
        static const int bufferSize = 1024;

        const size_t oldSize = data.size();
        data.resize(data.size() + bufferSize);

        bytesRead=recv(sock_descr,&data[oldSize],bufferSize,0);//bufferSize

        //log("read portion "+string(data.begin(), data.end()));

        data.resize(oldSize + bytesRead);

        string t=string(data.begin(), data.end());

        if(t.find(DAEMON_MSG_END) != string::npos){
            return t.substr(0,t.find(DAEMON_MSG_END));
        }

    } while (bytesRead > 0);


    return string (data.begin(), data.end());;
}

// ----------------------------------------------------------------


string sendCommand_sync(json command){

    string j=command.dump();

    log("command to send is - "+j);

    int wsz=write(sock_descr,j.c_str(),j.size());
    if(wsz == -1){
        log("command sending error");
        return "";
    }

    fsync(sock_descr);
    log("command was sended");

    string out= readReplyFromCommandServer();

    log("reply was received");

    //cout << out.c_str();
    //log(out);
    return out;
}


// ----------------------------------------------------------------

void sendCommand(json command){

    string j=command.dump();

    log("command to send is - "+j);

    int wsz=write(sock_descr,j.c_str(),j.size());
    if(wsz == -1){
        log("command sending error");
        return;
    }

    fsync(sock_descr);
    log("command was sended");

//    string out= readReplyFromCommandServer();

//    log("reply was received");

//    //cout << out.c_str();
//    //log(out);
//    return out;
}



// ----------------------------------------------------------------

void log(string mes){

    FILE* lf=fopen("/tmp/ccfw.log","a+");
    if(lf != NULL){

        time_t rawtime;
        struct tm * timeinfo;
        char buffer[80];

        time (&rawtime);
        timeinfo = localtime(&rawtime);

        strftime(buffer,sizeof(buffer),"%T - ",timeinfo);

        mes = string(buffer)+mes+" \n";
        fputs(mes.c_str(),lf);
        fclose(lf);
    }

//    string ns="echo "+mes+" >> /tmp/ccfw.log ";
//    system(ns.c_str());
    return;
}

// ----------------------------------------------------------------


int main(int argc, char *argv[])
{
    // argv[0]
    // argv[1] = socket name for communicate with command server
    // argv[2] = provider code
    // argv[3] = path to credentials
    // argv[4] = mount point


    // rebuild arguments list for fuse
    char* a[2];
    a[0] = argv[0];
    a[1] = argv[4];
    //a[2]="-f";//

    log("WORKER MOUNT POINT IS "+string(argv[4]));

    if(!connectToCommandServer(std::string(argv[1]))){//std::string(argv[1])
        return 1;
    }



    fuse_op.getattr = getattr_callback;
    //fuse_op.fgetattr = fgetattr_callback;
    fuse_op.open = open_callback;
    fuse_op.read = read_callback;
    fuse_op.readdir = readdir_callback;
    fuse_op.destroy = destroy_callback;
    fuse_op.write = write_callback;
    fuse_op.ftruncate = ftruncate_callback;
    fuse_op.truncate = truncate_callback;
    fuse_op.release= xmp_release;
    fuse_op.fsync = xmp_fsync;

    fuse_op.readlink = xmp_readlink;
    fuse_op.mknod = xmp_mknod;
    fuse_op.mkdir = xmp_mkdir;
    fuse_op.unlink = xmp_unlink;
    fuse_op.rmdir = xmp_rmdir;
    fuse_op.symlink= xmp_symlink;
    fuse_op.rename = xmp_rename;
    fuse_op.link = xmp_link;
    fuse_op.chmod = xmp_chmod;
    fuse_op.chown = xmp_chown;
    fuse_op.statfs = xmp_statfs;

    fuse_op.access = xmp_access;


    workSocket = argv[1];
    workProvider = argv[2];
    workPath = argv[3];
    mountPoint = argv[4];

    json comm;
    comm["command"] = "get_file_list";
    comm["params"]["socket"] = argv[1];
    comm["params"]["provider"] = argv[2];
    comm["params"]["path"] = argv[3];

    //json jsonFileList = json::parse( sendCommand_sync(comm) );
    sendCommand_sync(comm);


//            json jsonFileList;     // test
//            std::ifstream i("tst.json");

//            i >> jsonFileList;
//            //============================


    tempDirName="TESTING_TEMPORARY";//string(argv[1])+string("_")+getTempName();

    mkdir(string("/tmp/"+tempDirName).c_str(),0777);


//    for(json::iterator i = jsonFileList.begin(); i != jsonFileList.end(); i++){

//        MSFSObject obj;
//        json jo=i.value();
//        obj.state               = jo["state"];
//        obj.path                = jo["path"];
//        obj.fileName            = jo["fileName"];
//        obj.refCount            = -1;

//        //obj.remote.data         = jo["remote"]["data"];
//        obj.remote.exist        = true;
//        obj.remote.fileSize     = jo["remote"]["fileSize"];
//        obj.remote.md5Hash      = jo["remote"]["md5Hash"];
//        obj.remote.modifiedDate = jo["remote"]["modifiedDate"];
//        obj.remote.objectType   = jo["remote"]["objectType"];


//        fileList.insert(std::make_pair(i.key(),obj));


//    }



    listenerThreadParams.onIncomingCommand = NULL;
    listenerThreadParams.state = false;
    listenerThreadParams.socketName = string(workSocket+".incoming");
    pthread_create(&listenerThread,NULL,incomingListener,&listenerThreadParams);
    pthread_detach(listenerThread);

    sleep(1);

    if(!listenerThreadParams.state){

        return -1;
    }



    json comm2;
    comm2["command"] = "start_watcher";
    comm2["params"]["socket"] = argv[1];
    comm2["params"]["provider"] = argv[2];
    comm2["params"]["path"] = argv[3];
    comm2["params"]["watchPath"] = string("/tmp/"+tempDirName);

    sendCommand(comm2);

    return fuse_main(2, a, &fuse_op, NULL);
}



