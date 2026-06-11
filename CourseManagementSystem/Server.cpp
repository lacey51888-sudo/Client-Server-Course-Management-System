#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")

#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <mutex>
#include <algorithm>
#include <cctype>

#include "Database.h"

using namespace std;

#define PORT 50000
#define KEY 'K'
#define DATA_FILE "courses.txt"

Database db;                 // Real database module from Database.cpp
mutex version_mutex;
mutex log_mutex;
int database_version = 1;     // Used by clients to auto-sync updates

void xor_encrypt_decrypt(string& data) {
    for (size_t i = 0; i < data.size(); i++) {
        data[i] ^= KEY;
    }
}

string trim(const string& s) {
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == string::npos) return "";
    size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

string to_upper_copy(string s) {
    transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) {
        return (char)toupper(ch);
    });
    return s;
}

string to_lower_copy(string s) {
    transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) {
        return (char)tolower(ch);
    });
    return s;
}

bool contains_ignore_case(const string& text, const string& keyword) {
    return to_lower_copy(text).find(to_lower_copy(keyword)) != string::npos;
}

void log_msg(const string& msg) {
    lock_guard<mutex> lock(log_mutex);
    ofstream fout("server.log", ios::app);
    fout << msg << endl;
}

vector<string> split(const string& s, char delim) {
    vector<string> result;
    stringstream ss(s);
    string item;
    while (getline(ss, item, delim)) result.push_back(trim(item));
    return result;
}

string course_to_string(const Course& c) {
    // Keep the first 6 fields compatible with GUI_Client.cpp.
    // Semester is kept as the 7th field so it is not lost.
    return c.code + "|" + c.title + "|" + c.section + "|" +
           c.instructor + "|" + c.time + "|" + c.classroom + "|" + c.semester;
}

bool is_admin(const string& role) {
    return role == "admin";
}

bool has_empty_field(const vector<string>& f) {
    for (size_t i = 0; i < f.size(); i++) {
        if (trim(f[i]).empty()) return true;
    }
    return false;
}

void increase_version() {
    lock_guard<mutex> lock(version_mutex);
    database_version++;
}

int get_version() {
    lock_guard<mutex> lock(version_mutex);
    return database_version;
}

string handle_login(const string& req, string& current_role) {
    string cmd, role, username, password;
    stringstream ss(req);
    ss >> cmd >> role >> username >> password;

    if (cmd != "LOGIN" || role.empty() || username.empty() || password.empty()) {
        return "ERROR LoginFormat";
    }

    // Test accounts for presentation.
    // Student: username=student, password=student
    // Admin:   username=admin,   password=123456
    if (role == "student" && username == "student" && password == "student") {
        current_role = "student";
        return "SUCCESS\nstudent";
    }

    if (role == "admin" && username == "admin" && password == "123456") {
        current_role = "admin";
        return "SUCCESS\nadmin";
    }

    return "ERROR LoginFailed";
}

string make_course_list_response(const vector<Course>& courses) {
    if (courses.empty()) return "ERROR NoResult";

    string res = "SUCCESS\n";
    for (size_t i = 0; i < courses.size(); i++) {
        res += course_to_string(courses[i]);
        if (i != courses.size() - 1) res += "\n";
    }
    return res;
}

string process_request(const string& req, string& current_role) {
    string request = trim(req);

    if (request.find("LOGIN") == 0) {
        return handle_login(request, current_role);
    }

    if (request == "VERSION") {
        return "SUCCESS\n" + to_string(get_version());
    }

    if (request == "QUERY ALL") {
        return make_course_list_response(db.getAllCourses());
    }

    if (request.find("QUERY BY_CODE") == 0) {
        string code = trim(request.substr(14));
        if (code.empty()) return "ERROR EmptyCourseCode";

        vector<Course> result = db.searchByCode(code);
        if (result.empty()) return "ERROR CourseNotFound";
        return make_course_list_response(result);
    }

    if (request.find("QUERY BY_INSTRUCTOR") == 0) {
        string name = trim(request.substr(20));
        if (name.empty()) return "ERROR EmptyInstructor";

        vector<Course> all = db.getAllCourses();
        vector<Course> result;
        for (const auto& c : all) {
            if (contains_ignore_case(c.instructor, name)) {
                result.push_back(c);
            }
        }
        if (result.empty()) return "ERROR NoResult";
        return make_course_list_response(result);
    }

    if (request.find("QUERY BY_SEMESTER") == 0) {
        string semester = trim(request.substr(17));
        if (semester.empty()) return "ERROR EmptySemester";

        vector<Course> result = db.searchBySemester(semester);
        if (result.empty()) return "ERROR NoResult";
        return make_course_list_response(result);
    }

    // Bonus: advanced search by time slot, e.g. QUERY BY_TIME Mon or QUERY BY_TIME 10:00
    if (request.find("QUERY BY_TIME") == 0) {
        string keyword = trim(request.substr(13));
        if (keyword.empty()) return "ERROR EmptyTimeKeyword";

        vector<Course> result = db.searchByTime(keyword);
        if (result.empty()) return "ERROR NoResult";
        return make_course_list_response(result);
    }

    // Bonus: advanced search by section, e.g. QUERY BY_SECTION A
    if (request.find("QUERY BY_SECTION") == 0) {
        string section = trim(request.substr(16));
        if (section.empty()) return "ERROR EmptySection";

        vector<Course> result = db.searchBySection(section);
        if (result.empty()) return "ERROR NoResult";
        return make_course_list_response(result);
    }

    // Only admin can run update commands.
    if (request.find("UPDATE") == 0 && !is_admin(current_role)) {
        return "ERROR PermissionDenied AdminOnly";
    }

    // GUI format: UPDATE ADD code|title|section|instructor|time|room
    // Also accepts: UPDATE ADD code|title|section|instructor|time|room|semester
    if (request.find("UPDATE ADD") == 0) {
        vector<string> f = split(request.substr(11), '|');
        if (f.size() != 6 && f.size() != 7) {
            return "ERROR Format Use: UPDATE ADD code|title|section|instructor|time|room|semester";
        }
        if (has_empty_field(f)) return "ERROR EmptyField";

        if (!db.searchByCode(f[0]).empty()) {
            return "ERROR DuplicateCourseCode";
        }

        Course c;
        c.code = f[0];
        c.title = f[1];
        c.section = f[2];
        c.instructor = f[3];
        c.time = f[4];
        c.classroom = f[5];
        c.semester = (f.size() == 7) ? f[6] : "2026S1";

        db.addCourse(c);
        increase_version();
        return "SUCCESS\nCourse added. Version=" + to_string(get_version());
    }

    // GUI format: UPDATE MODIFY code|field|value
    if (request.find("UPDATE MODIFY") == 0) {
        vector<string> f = split(request.substr(14), '|');
        if (f.size() != 3) return "ERROR Format Use: UPDATE MODIFY code|field|value";
        if (f[0].empty() || f[1].empty() || f[2].empty()) return "ERROR EmptyField";

        vector<Course> found = db.searchByCode(f[0]);
        if (found.empty()) return "ERROR CourseNotFound";

        Course c = found[0];
        string field = to_lower_copy(f[1]);
        if (field == "title") c.title = f[2];
        else if (field == "section") c.section = f[2];
        else if (field == "instructor") c.instructor = f[2];
        else if (field == "time") c.time = f[2];
        else if (field == "classroom" || field == "room") c.classroom = f[2];
        else if (field == "semester") c.semester = f[2];
        else return "ERROR InvalidField";

        if (!db.updateCourse(f[0], c)) return "ERROR CourseNotFound";
        increase_version();
        return "SUCCESS\nCourse modified. Version=" + to_string(get_version());
    }

    if (request.find("UPDATE DELETE") == 0) {
        string code = trim(request.substr(14));
        if (code.empty()) return "ERROR EmptyCourseCode";

        if (!db.deleteCourse(code)) return "ERROR CourseNotFound";
        increase_version();
        return "SUCCESS\nCourse deleted. Version=" + to_string(get_version());
    }

    return "ERROR UnknownCommand";
}

void handle_client(SOCKET client_sock, sockaddr_in client_addr) {
    char buffer[8192];
    string current_role = "guest";  // guest / student / admin

    printf("Client connected: %s\n", inet_ntoa(client_addr.sin_addr));

    while (true) {
        int len = recv(client_sock, buffer, sizeof(buffer), 0);

        if (len <= 0) {
            printf("Client disconnected\n");
            closesocket(client_sock);
            return;
        }

        string request(buffer, len);
        xor_encrypt_decrypt(request);  // decrypt client request

        if (request != "VERSION") {
            log_msg("ROLE: " + current_role + " | REQ: " + request);
        }

        string response = process_request(request, current_role);

        if (request != "VERSION") {
            log_msg("ROLE: " + current_role + " | RES: " + response);
        }

        xor_encrypt_decrypt(response); // encrypt server response
        send(client_sock, response.c_str(), (int)response.size(), 0);
    }
}

int main() {
    // Load real course data before starting the server.
    db.loadFromFile(DATA_FILE);
    printf("Loaded course data from %s\n", DATA_FILE);

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("WSAStartup failed\n");
        return -1;
    }

    SOCKET server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock == INVALID_SOCKET) {
        printf("Socket creation failed\n");
        WSACleanup();
        return -1;
    }

    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_sock, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        printf("Bind failed. Maybe port %d is already in use.\n", PORT);
        closesocket(server_sock);
        WSACleanup();
        return -1;
    }

    listen(server_sock, 5);
    printf("Server running on port %d...\n", PORT);
    printf("Accounts: student/student, admin/123456\n");
    printf("Using real Database.cpp + %s\n", DATA_FILE);

    while (true) {
        sockaddr_in client_addr;
        int addr_len = sizeof(client_addr);

        SOCKET client_sock = accept(server_sock, (sockaddr*)&client_addr, &addr_len);
        if (client_sock == INVALID_SOCKET) continue;

        thread t(handle_client, client_sock, client_addr);
        t.detach();
    }

    closesocket(server_sock);
    WSACleanup();
    return 0;
}
