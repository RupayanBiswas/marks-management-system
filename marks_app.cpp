#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <map> // Ensure this is at the top of your file!
#include <sqlite3.h>

// Connects to DB and executes queries
sqlite3* db;
void initDB() {
    sqlite3_open("marks.db", &db);
    string sql = "CREATE TABLE IF NOT EXISTS students("
                 "cls INT, roll INT, name TEXT, bengali TEXT, english TEXT, maths TEXT, total INT, rank INT);";
    sqlite3_exec(db, sql.c_str(), NULL, 0, NULL);
}

// Example: Retrieve students for a specific class
vector<Student> getStudentsByClass(int cls) {
    vector<Student> students;
    sqlite3_stmt* stmt;
    string sql = "SELECT * FROM students WHERE cls = ?;";
    sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, cls);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        students.push_back({
            sqlite3_column_int(stmt, 0), sqlite3_column_int(stmt, 1), 
            (const char*)sqlite3_column_text(stmt, 2),
            (const char*)sqlite3_column_text(stmt, 3), (const char*)sqlite3_column_text(stmt, 4),
            (const char*)sqlite3_column_text(stmt, 5), sqlite3_column_int(stmt, 6), sqlite3_column_int(stmt, 7)
        });
    }
    sqlite3_finalize(stmt);
    return students;
}
using namespace std;

struct Student {
    int cls, roll, total, rank;
    string name, bengali, english, maths;
};

// Reads the entire CSV into a vector of Students
vector<Student> readDatabase() {
    vector<Student> students;
    ifstream file("central-database.csv");
    if (!file.is_open()) return students;

    string line;
    // Skip the header line
    getline(file, line);

    while (getline(file, line)) {
        if (line.empty()) continue;
        stringstream ss(line);
        string s_cls, s_roll, name, bengali, english, maths, s_total, s_rank;

        getline(ss, s_cls, ',');
        getline(ss, s_roll, ',');
        getline(ss, name, ',');
        getline(ss, bengali, ',');
        getline(ss, english, ',');
        getline(ss, maths, ',');
        getline(ss, s_total, ',');
        getline(ss, s_rank, ',');

        Student s;
        s.cls = stoi(s_cls);
        s.roll = stoi(s_roll);
        s.name = name;
        s.bengali = bengali;
        s.english = english;
        s.maths = maths;
        s.total = stoi(s_total);
        s.rank = stoi(s_rank);

        students.push_back(s);
    }
    file.close();
    return students;
}

// Rewrites the entire CSV with the updated vector data
void writeDatabase(const vector<Student>& students) {
    ofstream file("central-database.csv");
    file << "class,roll_no,full_name,bengali,english,maths,total,rank\n";
    for (const auto& s : students) {
        file << s.cls << ","
             << s.roll << ","
             << s.name << ","
             << s.bengali << ","
             << s.english << ","
             << s.maths << ","
             << s.total << ","
             << s.rank << "\n";
    }
    file.close();
}

// Helper to safely get the target subject mark reference out of a Student record
string& getSubjectMark(Student& s, const string& subject) {
    if (subject == "bengali") return s.bengali;
    if (subject == "english") return s.english;
    return s.maths; // defaults to maths
}

// Gets last roll from index.csv, updates it, and returns the new roll
int getNextRoll(int cls) {
    map<int, int> index;
    ifstream in("index.csv");
    int c, r;
    while (in >> c >> r) index[c] = r;
    in.close();

    index[cls]++;
    
    ofstream out("index.csv");
    for (auto const& [cl, roll] : index) out << cl << " " << roll << "\n";
    
    return index[cls];
}

// --- Watcher & Publishing Logic ---
void publishResults(int cls, vector<Student>& students) {
    // 1. Calculate Grand Totals for each student
    for (auto& s : students) {
        s.total = stoi(s.bengali) + stoi(s.english) + stoi(s.maths);
    }

    // 2. Sort by Total (Descending). If Total is a tie, sort by Roll Number (Ascending)
    sort(students.begin(), students.end(), [](const Student& a, const Student& b) {
        if (a.total != b.total) {
            return a.total > b.total; 
        }
        return a.roll < b.roll; // Tie-breaker condition
    });

    // 3. Assign Ranks (Handling Duplicate Totals gracefully)
    int currentRank = 1;
    for (size_t i = 0; i < students.size(); ++i) {
        if (i > 0 && students[i].total < students[i-1].total) {
            currentRank = i + 1; // Standard competition ranking (e.g., 1, 2, 2, 4)
        }
        students[i].rank = currentRank;
    }

    // 4. Generate the Timestamp
    auto now = chrono::system_clock::to_time_t(chrono::system_clock::now());
    char timeStr[26];
    ctime_r(&now, timeStr);
    string timestamp(timeStr);
    if (!timestamp.empty() && timestamp.back() == '\n') {
        timestamp.pop_back(); // Clean up trailing newline from ctime
    }

    // 5. Format the Pretty Print output string block
    stringstream buffer;
    buffer << "========================================================================================\n";
    buffer << "PUBLISHED RESULT FOR CLASS " << cls << " | Released: " << timestamp << "\n";
    buffer << "========================================================================================\n";
    buffer << left << setw(8)  << "Rank" 
           << setw(25) << "Full Name" 
           << setw(10) << "Roll No" 
           << setw(15) << "Bengali Marks" 
           << setw(15) << "English Marks" 
           << setw(20) << "Mathematics Marks" 
           << setw(15) << "Grand Total" << "\n";
    buffer << "----------------------------------------------------------------------------------------\n";
           
    for (const auto& s : students) {
        buffer << left << setw(8)  << s.rank 
               << setw(25) << s.name 
               << setw(10) << s.roll 
               << setw(15) << s.bengali 
               << setw(15) << s.english 
               << setw(20) << s.maths 
               << setw(15) << s.total << "\n";
    }
    buffer << "========================================================================================\n\n";

    string tsvPath = string("student-database/central-database-class-") + (cls < 10 ? "0" : "") + to_string(cls) + ".txt";

    ifstream inFile(tsvPath);
    if (inFile.is_open()) {
        buffer << inFile.rdbuf(); // Slurp older history underneath our new buffer block
        inFile.close();
    }
    
    // Write back entire composite string stream
    ofstream outFile(tsvPath);
    if (outFile.is_open()) {
        outFile << buffer.str();
        outFile.close();
        cout << "[WATCHER] Success! Prepend publishing operation complete on " << tsvPath << "\n";
    } else {
        cerr << "[ERROR] Could not open " << tsvPath << " for publishing records.\n";
    }
}


void checkPublishCondition(int cls) {
    // 1. Read the entire database in memory
    vector<Student> allStudents = readDatabase();
    
    // 2. Filter students belonging to the target class and check for missing marks
    vector<Student> classStudents;
    bool allMarksPresent = true;
    
    for (const auto& s : allStudents) {
        if (s.cls == cls) {
            classStudents.push_back(s);
            
            // If any subject mark is still "NULL", we cannot publish yet
            if (s.bengali == "NULL" || s.english == "NULL" || s.maths == "NULL") {
                allMarksPresent = false;
            }
        }
    }
    
    // 3. If the class has students and no marks are missing, trigger publication
    if (!classStudents.empty() && allMarksPresent) {
        cout << "\n[WATCHER] All marks submitted for Class " << cls << ". Generating final ranks...\n";
        publishResults(cls, classStudents);
        
        // 4. Update the main central-database.csv to save the calculated totals and ranks
        // First, map the newly updated totals/ranks back to the original database array
        for (auto& s : allStudents) {
            if (s.cls == cls) {
                for (const auto& updated : classStudents) {
                    if (s.roll == updated.roll) {
                        s.total = updated.total;
                        s.rank = updated.rank;
                        break;
                    }
                }
            }
        }
        writeDatabase(allStudents);
    } else {
        cout << "[WATCHER] Class " << cls << " still has pending/unsubmitted subject marks. Skipping publication.\n";
    }
}

// Returns a string. If the user wants to quit, it returns "Q".
string getValidMark() {
    string input;
    while (true) {
        cout << "Enter Mark (0-100) or 'q' to quit: ";
        cin >> input;

        // Check for quit condition
        if (input == "q" || input == "Q") {
            return "Q";
        }

        try {
            int mark = stoi(input);
            if (mark >= 0 && mark <= 100) {
                return input; // Valid mark
            } else {
                cout << "Error: Mark must be between 0 and 100.\n";
            }
        } catch (...) {
            cout << "Error: Invalid input. Enter a number or 'q'.\n";
        }
    }
}

// --- Menus ---

void teacherMenu(const string& subject) {
    int choice;
    int cls; // Shared variable scope for choices
    
    do {
        cout << "\n--- TEACHER MENU (" << subject << ") ---\n";
        cout << "1. Add Marks\n2. Update Marks\n3. Show Marks Details\n4. Logout\nChoice: ";
        if (!(cin >> choice)) {
            cin.clear();
            cin.ignore(10000, '\n');
            continue;
        }

        if (choice == 1) {
            cout << "Enter Class (1-12): "; 
            cin >> cls;

            vector<Student> students = readDatabase();
            bool updatedAny = false;

            cout << "\n--- Scanning for missing " << subject << " marks in Class " << cls << " ---\n";
            for (auto& s : students) {
                if (s.cls == cls) {
                    string& mark = getSubjectMark(s, subject);
                    
                    // Identify records where marks are missing (NULL)
                    if (mark == "NULL") {
                        cout << "Student: " << s.name << " (Roll No: " << s.roll << ")\n";
                        string result = getValidMark();
                        
                        if (result == "Q") {
                            cout << "Exiting entry loop.\n";
                            break; // Breaks the loop over students
                        }
                        mark = result; // Assign the validated mark
                        updatedAny = true;
                    }
                }
            }

            if (updatedAny) {
                writeDatabase(students);
                cout << "Database updated successfully.\n";
                checkPublishCondition(cls); // Top-level watcher trigger
            } else {
                cout << "No missing entries found for this subject in Class " << cls << ".\n";
            }

        } else if (choice == 2) {
            cout << "Enter Class (1-12): "; cin >> cls;
            int roll;
            cout << "Enter Student Roll No: "; cin >> roll;

            vector<Student> students = readDatabase();
            bool found = false;

            for (auto& s : students) {
                if (s.cls == cls && s.roll == roll) {
                    found = true;
                    string& mark = getSubjectMark(s, subject);
                    
                    cout << "\nStudent Found: " << s.name << "\n";
                    cout << "Current " << subject << " Marks: " << mark << "\n";
                    cout << "Proceed to update? (y/n): ";
                    char confirm;
                    cin >> confirm;

                    if (confirm == 'y' || confirm == 'Y') {
                        string result = getValidMark();
                        if (result != "Q") {
                            mark = result;
                            writeDatabase(students);
                            cout << "Marks updated successfully.\n";
                            checkPublishCondition(cls);
                        } else {
                            cout << "Update cancelled.\n";
                        }
                    }
                    break;
                }
            }
            if (!found) {
                cout << "Student with Roll No " << roll << " not found in Class " << cls << ".\n";
            }

        } else if (choice == 3) {
            cout << "Enter Class (1-12): "; cin >> cls;
            vector<Student> students = readDatabase();
            
            cout << "\n=============================================\n";
            cout << "  " << subject << " MARKS DETAILS FOR CLASS " << cls << "\n";
            cout << "=============================================\n";
            cout << left << setw(10) << "Roll No" << setw(25) << "Student Name" << setw(10) << "Marks" << "\n";
            cout << "---------------------------------------------\n";

            bool dynamicRecordsFound = false;
            for (const auto& s : students) {
                if (s.cls == cls) {
                    dynamicRecordsFound = true;
                    // Accessing only this teacher's subject marks field dynamically
                    string displayMark = (subject == "bengali") ? s.bengali : 
                                         (subject == "english") ? s.english : s.maths;
                                         
                    cout << left << setw(10) << s.roll 
                         << setw(25) << s.name 
                         << setw(10) << displayMark << "\n";
                }
            }
            if (!dynamicRecordsFound) {
                cout << "No student records found for Class " << cls << ".\n";
            }
            cout << "=============================================\n";
        }
        
    } while (choice != 4);
    cout << "Logging out from teacher profile...\n";
}

void adminMenu(const string& user) {
    int choice;
    do {
        cout << "\n--- ADMIN MENU (" << user << ") ---\n";
        cout << "1. Add Entry\n2. Update Entry\n3. Show Details\n4. Logout\nChoice: ";
        cin >> choice;

        if (choice == 1) {
            int cls, count;
            cout << "Enter Class (1-12): "; cin >> cls;
            cout << "How many students to add? "; cin >> count;
            
            vector<Student> students = readDatabase();
            for (int i = 0; i < count; ++i) {
                int newRoll = getNextRoll(cls);
                string name;
                cout << "Enter Name for Roll " << newRoll << ": ";
                cin >> ws; getline(cin, name);
                
                students.push_back({cls, newRoll, 0, 0, name, "NULL", "NULL", "NULL"});
            }
            writeDatabase(students);
            cout << "Successfully added " << count << " students.\n";
            
        } else if (choice == 2) {
            int cls, roll;
            cout << "Enter Class: "; cin >> cls;
            cout << "Enter Roll No: "; cin >> roll;

            vector<Student> students = readDatabase();
            bool found = false;
            for (auto& s : students) {
                if (s.cls == cls && s.roll == roll) {
                    found = true;
                    cout << "Found: " << s.name << " (Roll: " << s.roll << ")\n";
                    cout << "Do you want to update the name? (y/n): ";
                    char conf; cin >> conf;
                    if (conf == 'y' || conf == 'Y') {
                        cout << "Enter Updated Name: ";
                        cin >> ws; getline(cin, s.name);
                        writeDatabase(students);
                        cout << "Student name updated.\n";
                    }
                    break;
                }
            }
            if (!found) cout << "Student not found!\n";
        } else if (choice == 3) {
            vector<Student> students = readDatabase();
            cout << left << setw(10) << "Class" << setw(10) << "Roll" << "Name" << endl;
            for (const auto& s : students) 
                cout << left << setw(10) << s.cls << setw(10) << s.roll << s.name << endl;
        }
    } while (choice != 4);
}


int main(int argc, char* argv[]) {
    if (argc < 2) return 1;
    string username = argv[1];

    if (username == "admin") {
        adminMenu(username);
    } else if (username == "bengali" || username == "english" || username == "maths") {
        teacherMenu(username);
    } else {
        cout << "Role not recognized.\n";
    }
    return 0;
}

