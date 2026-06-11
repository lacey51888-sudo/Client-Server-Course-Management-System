#include "Database.h"
#include <fstream>
#include <sstream>
#include <algorithm>

//read file
void Database::loadFromFile(const string& filename)
{
    lock_guard<recursive_mutex> lock(db_mutex);
    ifstream fin(filename);

    if (!fin)
    {
        cout << "Cannot open file!" << endl;
        return;
    }

    courses.clear();

    string line;
    
    while (getline(fin, line)) {
        if (line.empty()) continue; 
        stringstream ss(line);
        Course c;

        
        if (!getline(ss, c.code, '|') || !getline(ss, c.title, '|') ||
            !getline(ss, c.section, '|') || !getline(ss, c.instructor, '|') ||
            !getline(ss, c.time, '|') || !getline(ss, c.classroom, '|') ||
            !getline(ss, c.semester)) {
            continue; 
        }
        courses.push_back(c);
    }

    fin.close();

    cout << "Data loaded successfully!" << endl;
}

//save file
void Database::saveToFile(const string& filename)
{
    lock_guard<recursive_mutex> lock(db_mutex);
    ofstream fout(filename);

    if (!fout) {
        cout << "Error: Cannot open file for writing!" << endl;
        return;
    }

    for (Course c : courses)
    {
        fout
            << c.code << "|"
            << c.title << "|"
            << c.section << "|"
            << c.instructor << "|"
            << c.time << "|"
            << c.classroom << "|"
            << c.semester
            << endl;
    }

    fout.close();
}

//change courses
//1.add course
void Database::addCourse(const Course& c)
{
    lock_guard<recursive_mutex> lock(db_mutex);
    courses.push_back(c);
    saveToFile("courses.txt");
}

//2.delete course
bool Database::deleteCourse(const string& code)
{
    lock_guard<recursive_mutex> lock(db_mutex);
    for (size_t i = 0; i < courses.size(); i++)
    {
        if (courses[i].code == code)
        {
            courses.erase(courses.begin() + i);
            saveToFile("courses.txt");
            return true;
        }
    }

    return false;
}

//3.update course
bool Database::updateCourse(const string& code, const Course& newCourse)
{
    lock_guard<recursive_mutex> lock(db_mutex);
    for (size_t i = 0; i < courses.size(); i++)
    {
        if (courses[i].code == code)
        {
            courses[i] = newCourse;
            saveToFile("courses.txt");
            return true;
        }
    }

    return false;
}

//Serch function
//1.search by code
vector<Course> Database::searchByCode(const string& code)
{
    lock_guard<recursive_mutex> lock(db_mutex);
    vector<Course> result;

    for (Course c : courses)
    {
        if (c.code == code)
        {
            result.push_back(c);
        }
    }

    return result;
}

//2.search by instructor
vector<Course> Database::searchByInstructor(const string& instructor)
{
    lock_guard<recursive_mutex> lock(db_mutex);
    vector<Course> result;

    for (Course c : courses)
    {
        if (c.instructor == instructor)
        {
            result.push_back(c);
        }
    }

    return result;
}

//3.search by semester
vector<Course> Database::searchBySemester(const string& semester)
{
    lock_guard<recursive_mutex> lock(db_mutex);
    vector<Course> result;

    for (Course c : courses)
    {
        if (c.semester == semester)
        {
            result.push_back(c);
        }
    }

    return result;
}

//Advanced search
//1.search by time
vector<Course> Database::searchByTime(const string& keyword)
{
    lock_guard<recursive_mutex> lock(db_mutex);
    vector<Course> result;

    string upKeyword = keyword;
    transform(upKeyword.begin(), upKeyword.end(), upKeyword.begin(), ::toupper);

    for (const auto& c : courses) {
        string upTime = c.time;
        transform(upTime.begin(), upTime.end(), upTime.begin(), ::toupper);
        if (upTime.find(upKeyword) != string::npos) {
            result.push_back(c);
        }
    }
    return result;
}


//2.search by section
vector<Course> Database::searchBySection(const string& section)
{
    lock_guard<recursive_mutex> lock(db_mutex);
    vector<Course> result;

    for (Course c : courses)
    {
        if (c.section == section)
        {
            result.push_back(c);
        }
    }

    return result;
}


vector<Course> Database::getAllCourses()
{
    lock_guard<recursive_mutex> lock(db_mutex);
    return courses;
}

void Database::displayAll()
{
    lock_guard<recursive_mutex> lock(db_mutex);
    for (Course c : courses)
    {
        cout
            << c.code << " "
            << c.title << " "
            << c.section << " "
            << c.instructor << " "
            << c.time << " "
            << c.classroom << " "
            << c.semester
            << endl;
    }
}

//communication protocol
string Database::processRequest(const string& request) {
    lock_guard<recursive_mutex> lock(db_mutex);
    stringstream ss(request);
    string command;
    ss >> command;

    // 1. search function
    if (command == "QUERY") {
        string code;
        ss >> code;
        vector<Course> result = searchByCode(code);
        if (result.empty()) return "FAILURE|Course Not Found";

        string output = "RESULT";
        for (const auto& c : result) {
            output += "|" + c.code + "," + c.title + "," + c.section + "," +
                c.instructor + "," + c.time + "," + c.classroom + "," + c.semester;
        }
        return output;
    }

    // 2.login function
    else if (command == "LOGIN") {
        string user, pass;
        ss >> user >> pass;
        if (user == "admin" && pass == "password") return "SUCCESS"; 
        return "FAILURE";
    }

    // 3.uodate function
    else if (command == "UPDATE") {
        string code, field, newValue;

        //get course code
        if (!(ss >> code >> field)) {
            return "FAILURE|Missing Parameters";
        }

        getline(ss >> ws, newValue);

        if (newValue.empty()) {
            return "FAILURE|New Value Cannot Be Empty";
        }

        bool found = false;
        for (auto& c : courses) {
            if (c.code == code) {
                //update
                if (field == "TITLE") c.title = newValue;
                else if (field == "SECTION") c.section = newValue;
                else if (field == "INSTRUCTOR") c.instructor = newValue;
                else if (field == "TIME") c.time = newValue;
                else if (field == "ROOM" || field == "CLASSROOM") c.classroom = newValue;
                else if (field == "SEMESTER") c.semester = newValue;
                else return "FAILURE|Invalid Field Name";

                found = true;
                break;
            }
        }

        if (found) {
            //save
            saveToFile("courses.txt");
            return "OK";
        }
        else {
            return "FAILURE|Course Not Found";
        }
    }

    // 4.add function(ADD <Code>|<Title>|<Section>|<Instructor>|<Time>|<Room>|<Semester>)
    else if (command == "ADD") {
        string remaining;
        getline(ss >> ws, remaining);
        stringstream dataStream(remaining);
        Course c;
        
        if (getline(dataStream, c.code, '|') &&
            getline(dataStream, c.title, '|') &&
            getline(dataStream, c.section, '|') &&
            getline(dataStream, c.instructor, '|') &&
            getline(dataStream, c.time, '|') &&
            getline(dataStream, c.classroom, '|') &&
            getline(dataStream, c.semester)) {

            for (const auto& existing : courses) {
                if (existing.code == c.code) {
                    return "FAILURE|Duplicate Course Code";
                }
            }

            courses.push_back(c);
            saveToFile("courses.txt");
            return "OK";
        }
        return "FAILURE|Invalid ADD Format. Use: ADD Code|Title|Section|Instructor|Time|Room|Semester";
    }

    // 5.delete function(DELETE <Code>)
    else if (command == "DELETE") {
        string code;
        ss >> code;
        for (auto it = courses.begin(); it != courses.end(); ++it) {
            if (it->code == code) {
                courses.erase(it);
                saveToFile("courses.txt");
                return "OK";
            }
        }
        return "FAILURE";
    }

    return "FAILURE|Course Code Not Found";
}