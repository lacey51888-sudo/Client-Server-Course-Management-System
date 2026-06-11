# Client-Server-Course-Management-System
=====================================

This project is a Windows-based client-server course timetable system.

The server uses the real database implementation in Database.cpp and loads course data from courses.txt.
The GUI client connects to the server and allows students and admins to query or manage course information.

Required Files in This Package
------------------------------
- Server.cpp              : Windows server source code
- GUI_Client.cpp          : Windows GUI client source code
- Database.cpp            : Real database implementation
- Database.h              : Real database header file
- courses.txt             : Course data file
- README_WINDOWS_TEST.txt : Instructions for compilation, running, and testing

Note:
- server.exe and GUI_Client.exe are generated after compilation.
- They are not included in this source code package.
- courses.txt must stay in the same folder as server.exe when running the program.


Compilation Method
------------------
Recommended method on a lab Windows computer with MinGW g++ installed:

1. Copy the whole project folder to Desktop or C:\dcn.

2. Open PowerShell or CMD in this project folder.

   For example, if the folder is C:\dcn, run:

   cd C:\dcn

3. Compile the server:

   g++ -std=c++17 Server.cpp Database.cpp -o server.exe -lws2_32

4. Compile the GUI client:

   g++ -std=c++17 GUI_Client.cpp -o GUI_Client.exe -mwindows -lws2_32 -lcomctl32

5. After successful compilation, the following executable files will be generated:

   server.exe
   GUI_Client.exe

Note:
- The GUI client uses Windows Common Controls, so -lcomctl32 is required.
- If -lcomctl32 is missing, the GUI client may fail to compile with an error related to InitCommonControlsEx.


Running Method
--------------
1. Start the server first.

   If using PowerShell, run:

   .\server.exe

   If using CMD, run:

   server.exe

2. Keep the server window open.

3. Open another PowerShell or CMD window in the same project folder.

4. Start the GUI client.

   If using PowerShell, run:

   .\GUI_Client.exe

   If using CMD, run:

   GUI_Client.exe

Important:
- The server must be started before the GUI client.
- The port used by the server is 50000.
- If port 50000 is already used, close any old server.exe windows and try again.
- courses.txt must stay in the same folder as server.exe.


Test Accounts
-------------
Student account:
Role: student
Username: student
Password: student

Admin account:
Role: admin
Username: admin
Password: 123456


Main Functions to Test
----------------------

1. Student login test

After logging in as a student, test the following functions:

- View all courses
- Query course by course code
- Query course by instructor
- Query all courses for a given semester, for example:
  2026S1
- Advanced search by time keyword, for example:
  Mon
  10:00
- Advanced search by section, for example:
  A

Expected result:
- Students can view and search course information.
- Students cannot add, modify, or delete courses.


2. Admin login test

After logging in as an admin, test the following functions:

- Add course
- Modify course
- Delete course
- Check that courses.txt is updated after add, modify, or delete operations.

Example course for testing:

Code: TEST101
Title: Test Course
Section: A1
Instructor: Dr.Test
Time: Mon10:00-12:00
Room: Room999
Semester: 2026S1

Expected result:
- Admin can add, modify, and delete course records.
- The changes are saved into courses.txt.


3. Auto-sync test

Steps:

1. Keep one server.exe window open.
2. Open two GUI_Client.exe windows.
3. Login one client as student.
4. Login the other client as admin.
5. Admin adds, modifies, or deletes a course.
6. Student side checks the database version every 1 second and refreshes automatically when an update is detected.

Expected result:
- The student client can see the updated course data automatically after the admin changes the database.


4. Five-client concurrency test

Steps:

1. Keep one server.exe window open.
2. Open five GUI_Client.exe windows.
3. Login all five clients as student.
4. Test different query functions on different clients.

Expected result:
- The server should respond to all five clients without crashing.


Communication Protocol
----------------------
All messages are text-based and protected by a simple XOR encryption key K before transmission.

Request format:

LOGIN <role> <username> <password>
VERSION
QUERY ALL
QUERY BY_CODE <course_code>
QUERY BY_INSTRUCTOR <instructor_keyword>
QUERY BY_SEMESTER <semester>
QUERY BY_TIME <time_keyword>
QUERY BY_SECTION <section>
UPDATE ADD <code>|<title>|<section>|<instructor>|<time>|<classroom>|<semester>
UPDATE MODIFY <code>|<field>|<value>
UPDATE DELETE <course_code>

Response format:

SUCCESS
<result data or message>

ERROR <reason>

