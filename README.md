## Project Progress. Phase II

SmartCare is a healthcare management system that we are building as part of our Project-Based Learning (PBL). The project relies on Object‑Oriented Programming (OOP) Data Structures and Algorithms (DSA) and PostgreSQL to keep track of healthcare records.

### Completed Work

- We have built the C++ application with a menu‑driven interface.

- We have added patient management features, such as adding, showing and searching records.

- We have added doctor management and specialization records.

- We have added appointment booking that uses a queue.

- We have added emergency case management that uses a priority queue.

- We use a linked list for records an unordered map for searching patients a vector for doctor records and a stack for recent actions.

- We have linked PostgreSQL with the C++ application so that records can be stored and retrieved.

- We have created database tables for patients, doctors, appointments, emergency cases and medical records.

- We have generated patient and doctor datasets with Python for testing purposes.

- We have written a PowerShell script that exports the PostgreSQL database to an SQL snapshot.

- We have successfully tested uploading the database snapshot to GitHub.

### Database Integration

PostgreSQL is the database for SmartCare. Records that SmartCare adds through the application are stored in PostgreSQL after each database operation.

SmartCare also includes a database export script that creates an SQL snapshot and then uploads changes to GitHub using Git.

The current synchronization workflow is:

1. The SmartCare C++ application performs database operations.

2. PostgreSQL stores the records.

3. The PowerShell script exports the database.

4. Git tracks changes to the SQL snapshot.

5. The updated snapshot is pushed to GitHub.

The synchronization script has been tested manually. We still need to set up and test execution using Windows Task Scheduler.

### Work in Progress

- and verify the import of the 50‑doctor synthetic dataset.

- Test retrieval and display of patient and doctor records, in SmartCare C++.

- Test appointment booking and emergency case handling with existing database records.

- Configure and test daily database synchronization using Windows Task Scheduler.

- Improve error handling and test SmartCare with different datasets.

- Finish project documentation. Prepare the final demonstration.

### Technology Stack

- **Programming Language:** C++

- **Programming Concepts:** Object‑Oriented Programming and Data Structures and Algorithms

- **Data Structures:** Linked List, Queue, Priority Queue, Stack, Vector and Unordered Map

- **Database:** PostgreSQL

- **Dataset Generation:** Python

- **Database Synchronization:** PowerShell, Git and GitHub

### Data Security

The project uses data for academic testing. Database snapshots must not contain patient information, passwords or other sensitive data. PostgreSQL remains the database while GitHub stores the source code and the versioned database snapshots.