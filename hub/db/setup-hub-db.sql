CREATE DATABASE picocenter;

CREATE TABLE picos ( pico_id INT NOT NULL AUTO_INCREMENT, hot BOOL NOT NULL, worker_id INT, public_ip VARCHAR(15), internal_ip VARCHAR(15) NOT NULL, ports TEXT NOT NULL, hostname VARCHAR(255) NOT NULL, customer_id INT NOT NULL, PRIMARY KEY (pico_id));
CREATE INDEX pico_index on picos (pico_id) USING BTREE;

CREATE TABLE workers ( worker_id INT NOT NULL AUTO_INCREMENT, status TINYINT NOT NULL, heart_ip VARCHAR(15) NOT NULL, PRIMARY KEY (worker_id));
CREATE INDEX worker_index on workers (worker_id) USING BTREE;

CREATE TABLE picoports (pico_id INT NOT NULL, port INT NOT NULL);
CREATE INDEX pico_index ON picoports (pico_id) USING BTREE;

CREATE TABLE ips (worker_id INT NOT NULL, ip VARCHAR(15) NOT NULL, status TINYINT NOT NULL);
CREATE INDEX worker_index ON ips (worker_id) USING BTREE;

CREATE TABLE meta (last_internal_ip VARCHAR(15) NOT NULL);
