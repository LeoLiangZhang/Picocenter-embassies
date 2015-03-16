CREATE DATABASE picocenter;
CREATE TABLE pico ( pico_id INT NOT NULL AUTO_INCREMENT, hot BOOL NOT NULL, worker_id INT, public_ip VARCHAR(15), internal_ip VARCHAR(15) NOT NULL, ports TEXT NOT NULL, hostname VARCHAR(255) NOT NULL, customer_id INT NOT NULL, PRIMARY KEY (pico_id));
CREATE TABLE workers ( worker_id INT NOT NULL AUTO_INCREMENT, status ENUM('available','overloaded','offline') NOT NULL, heart_ip VARCHAR(15) NOT NULL, heart_port VARCHAR(5) NOT NULL, PRIMARY KEY (worker_id));
CREATE TABLE addrmap( worker_id INT NOT NULL, public_ip VARCHAR(15) NOT NULL, ports_allocated TEXT NOT NULL, port_80_allocated BOOL NOT NULL, PRIMARY KEY (public_ip));
