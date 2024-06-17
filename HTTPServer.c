#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sqlite3.h>
#include <cjson/cJSON.h> 

#define PORT 3999
#define BUFFER_SIZE 4096

void handle_request(int new_socket);
void query_database(char *response);
void insert_into_database(const char *name, char *response);
void update_database(int id, const char *name, char *response);
void delete_from_database(int id, char *response);
void send_response(int socket, const char *header, const char *body);
void initialize_database();

int callback(void *data, int argc, char **argv, char **azColName) {
    int i;
    cJSON *json_array = (cJSON *)data;
    cJSON *json_row = cJSON_CreateObject();

    for (i = 0; i < argc; i++) {
        cJSON_AddStringToObject(json_row, azColName[i], argv[i] ? argv[i] : "NULL");
    }

    cJSON_AddItemToArray(json_array, json_row);
    return 0;
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    // Initialize the database and create table if it doesn't exist
    initialize_database();

    // Create socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Bind to the port
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_fd, 3) < 0) {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Server is listening on port %d\n", PORT);

    while (1) {
        // Accept an incoming connection
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept failed");
            close(server_fd);
            exit(EXIT_FAILURE);
        }

        // Handle the request
        handle_request(new_socket);

        // Close the socket
        close(new_socket);
    }

    close(server_fd);
    return 0;
}

void initialize_database() {
    sqlite3 *db;
    char *err_msg = 0;
    int rc = sqlite3_open("test.db", &db);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(EXIT_FAILURE);
    }

    const char *create_table_sql = "CREATE TABLE IF NOT EXISTS test ("
                                   "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                   "name TEXT NOT NULL);";

    rc = sqlite3_exec(db, create_table_sql, 0, 0, &err_msg);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        exit(EXIT_FAILURE);
    }

    //const char *insert_sql = "INSERT INTO test (name) VALUES ('Initial Data');";
    //rc = sqlite3_exec(db, insert_sql, 0, 0, &err_msg);

    //if (rc != SQLITE_OK) {
    //    fprintf(stderr, "SQL error: %s\n", err_msg);
    //    sqlite3_free(err_msg);
   // }

    sqlite3_close(db);
}

void handle_request(int new_socket) {
    char buffer[BUFFER_SIZE] = {0};
    char method[10], path[100], protocol[10];
    read(new_socket, buffer, BUFFER_SIZE);
    sscanf(buffer, "%s %s %s", method, path, protocol);
    printf("Received request: %s %s %s\n", method, path, protocol);

    if (strcmp(method, "GET") == 0) {
        char response[BUFFER_SIZE * 10];
        query_database(response);
        send_response(new_socket, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n", response);
    } else if (strcmp(method, "POST") == 0) {
    // Find the start of JSON data in the buffer
    char *json_start = strchr(buffer, '{');
       // printf(buffer);
    //printf(json_start);

    
    if (json_start == NULL) {
        // Handle error: JSON data not found
        send_response(new_socket, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n", "");
        return;
    }
    
    // Parse JSON data
    cJSON *json = cJSON_Parse(buffer);
    
    if (json == NULL) {
        // Handle error: JSON parsing failed
        send_response(new_socket, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n", "");
        return;
    }

    // Extract name from JSON object
    cJSON *name = cJSON_GetObjectItem(json, "name");
    if (name == NULL || !cJSON_IsString(name)) {
        cJSON_Delete(json);
        // Handle error: "name" field not found or not a string
        send_response(new_socket, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n", "");
        return;
    }

    // Insert data into the database and prepare response
    char response[BUFFER_SIZE] = {0};
    insert_into_database(name->valuestring, response);
    
    // Send success response with JSON content
    char http_response[BUFFER_SIZE];
    snprintf(http_response, sizeof(http_response), "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\n\r\n%s", strlen(response), response);
    send_response(new_socket, http_response, strlen(http_response));

    // Clean up cJSON object
    cJSON_Delete(json);
    } else if (strcmp(method, "PUT") == 0) {
        // Find the start of JSON data in the buffer
        char *json_start = strchr(buffer, '{');
        if (json_start == NULL) {
            // Handle error: JSON data not found
            send_response(new_socket, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n", "");
            return;
        }
        
        // Parse JSON data
        cJSON *json = cJSON_Parse(json_start);
        printf(json_start);
        if (json == NULL) {
            // Handle error: JSON parsing failed
            send_response(new_socket, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n", "");
            return;
        }

        // Extract id and name from JSON object
        cJSON *id = cJSON_GetObjectItem(json, "id");
        cJSON *name = cJSON_GetObjectItem(json, "name");
        if (id == NULL || !cJSON_IsNumber(id) || name == NULL || !cJSON_IsString(name)) {
            cJSON_Delete(json);
            // Handle error: "id" or "name" field not found or not of expected type
            send_response(new_socket, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n", "");
            return;
        }

        // Update data in the database and prepare response
        char response[BUFFER_SIZE] = {0};
        update_database(id->valueint, name->valuestring, response);
        
        // Send success response with JSON content
        char http_response[BUFFER_SIZE];
        snprintf(http_response, sizeof(http_response), "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\n\r\n%s", strlen(response), response);
        send_response(new_socket, http_response, strlen(http_response));

        // Clean up cJSON object
        cJSON_Delete(json);
    } else if (strcmp(method, "DELETE") == 0) {
         // Find the start of JSON data in the buffer
    char *json_start = strchr(buffer, '{');
    if (json_start == NULL) {
        // Handle error: JSON data not found
        send_response(new_socket, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n", "");
        return;
    }
    
    // Parse JSON data
    cJSON *json = cJSON_Parse(json_start);
    if (json == NULL) {
        // Handle error: JSON parsing failed
        send_response(new_socket, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n", "");
        return;
    }

    // Extract id from JSON object
    cJSON *id = cJSON_GetObjectItem(json, "id");
    if (id == NULL || !cJSON_IsNumber(id)) {
        cJSON_Delete(json);
        // Handle error: "id" field not found or not a number
        send_response(new_socket, "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n", "");
        return;
    }

    // Delete data from the database and prepare response
    char response[BUFFER_SIZE] = {0};
    delete_from_database(id->valueint, response);
    
    // Send success response with JSON content
    char http_response[BUFFER_SIZE];
    snprintf(http_response, sizeof(http_response), "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\n\r\n%s", strlen(response), response);
    send_response(new_socket, http_response, strlen(http_response));

    // Clean up cJSON object
    cJSON_Delete(json);
    } else {
        send_response(new_socket, "HTTP/1.1 400 Bad Request\r\nContent-Type: application/json\r\n\r\n", "{\"error\":\"400 Bad Request\"}");
    }
}


void query_database(char *response) {
    sqlite3 *db;
    char *err_msg = 0;
    int rc = sqlite3_open("test.db", &db);

    if (rc) {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        return;
    }

    const char *sql = "SELECT * FROM test;";
    cJSON *json_array = cJSON_CreateArray();
    rc = sqlite3_exec(db, sql, callback, json_array, &err_msg);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
    }

    char *json_str = cJSON_Print(json_array);
    strcpy(response, json_str);

    cJSON_Delete(json_array);
    free(json_str);
    sqlite3_close(db);
}

void insert_into_database(const char *name, char *response) {
        snprintf(response, BUFFER_SIZE, "{\"message\": \"Inserted %s into database\"}", name);

    sqlite3 *db;
    char *err_msg = 0;
    int rc = sqlite3_open("test.db", &db);

    if (rc) {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        return;
    }

    char sql[BUFFER_SIZE];
    snprintf(sql, sizeof(sql), "INSERT INTO test (name) VALUES ('%s');", name);
    rc = sqlite3_exec(db, sql, 0, 0, &err_msg);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "message", "Failed to insert record.");
        char *json_str = cJSON_Print(json);
        strcpy(response, json_str);
        cJSON_Delete(json);
        free(json_str);
    } else {
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "message", "Record inserted successfully.");
        char *json_str = cJSON_Print(json);
        strcpy(response, json_str);
        cJSON_Delete(json);
        free(json_str);
    }

    sqlite3_close(db);
}

void update_database(int id, const char *name, char *response) {
    sqlite3 *db;
    char *err_msg = 0;
    int rc = sqlite3_open("test.db", &db);

    if (rc) {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        return;
    }

    char sql[BUFFER_SIZE];
    snprintf(sql, sizeof(sql), "UPDATE test SET name='%s' WHERE id=%d;", name, id);
    rc = sqlite3_exec(db, sql, 0, 0, &err_msg);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "message", "Failed to update record.");
        char *json_str = cJSON_Print(json);
        strcpy(response, json_str);
        cJSON_Delete(json);
        free(json_str);
    } else {
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "message", "Record updated successfully.");
        char *json_str = cJSON_Print(json);
        strcpy(response, json_str);
        cJSON_Delete(json);
        free(json_str);
    }

    sqlite3_close(db);
}

void delete_from_database(int id, char *response) {
    sqlite3 *db;
    char *err_msg = 0;
    int rc = sqlite3_open("test.db", &db);

    if (rc) {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        return;
    }

    char sql[BUFFER_SIZE];
    snprintf(sql, sizeof(sql), "DELETE FROM test WHERE id=%d;", id);
    rc = sqlite3_exec(db, sql, 0, 0, &err_msg);

    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "message", "Failed to delete record.");
        char *json_str = cJSON_Print(json);
        strcpy(response, json_str);
        cJSON_Delete(json);
        free(json_str);
    } else {
        cJSON *json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "message", "Record deleted successfully.");
        char *json_str = cJSON_Print(json);
        strcpy(response, json_str);
        cJSON_Delete(json);
        free(json_str);
    }

    sqlite3_close(db);
}

void send_response(int socket, const char *header, const char *body) {
    write(socket, header, strlen(header));
    write(socket, body, strlen(body));
}
