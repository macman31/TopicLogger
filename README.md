# TopicLogger

TopicLogger is an open source IRC bot that logs your chats.

## Requirements

The bot portion of TopicLogger uses yaml-cpp, the MySQL 5 C library, and libircclient 1.7. However, there is a bug in the official release of libircclient 1.7, so you need to apply [this patch](http://pastebin.com/KDDkQMwy) to libircclient.c before compiling the library.