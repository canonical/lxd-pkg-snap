package main

import (
	"database/sql"

	_ "github.com/mattn/go-sqlite3"
)

type dbInstance struct {
	db *sql.DB
}

func dbOpen(path string) (*dbInstance, error) {
	// Open the database
	db, err := sql.Open("sqlite3", path)
	if err != nil {
		return nil, err
	}

	// Setup the internal struct
	d := &dbInstance{db: db}

	return d, nil
}

func (d *dbInstance) updateStoragePoolSource(pool string, path string) error {
	id := int64(-1)
	statement := `SELECT id FROM storage_pools WHERE name=?;`
	err := d.db.QueryRow(statement, pool).Scan(&id)
	if err != nil {
		return err
	}

	_, err = d.db.Exec("UPDATE storage_pools_config SET value=? WHERE key='source' AND storage_pool_id=?;", path, id)
	if err != nil {
		return err
	}

	return nil
}
