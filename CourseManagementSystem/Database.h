#pragma once
#include <iostream>
#include <vector>
#include <string>
#include <mutex>
using namespace std;

struct Course
{
    string code;
    string title;
    string section;
    string instructor;
    string time;
    string classroom;
    string semester;
};

class Database
{
private:
    //bonus1: Data Caching(memory database)
    vector<Course> courses;

    std::recursive_mutex db_mutex;

public:
    void loadFromFile(const string& filename);
    void saveToFile(const string& filename);

    void addCourse(const Course& c);
    bool deleteCourse(const string& code);
    bool updateCourse(const string& code, const Course& newCourse);

    vector<Course> searchByCode(const string& code);
    vector<Course> searchByInstructor(const string& instructor);
    vector<Course> searchBySemester(const string& semester);

    void displayAll();

    vector<Course> searchByTime(const string& keyword);
    vector<Course> searchBySection(const string& section);

    // Return a copy of all courses for server/GUI integration.
    vector<Course> getAllCourses();

    string processRequest(const string& request);
}; 
