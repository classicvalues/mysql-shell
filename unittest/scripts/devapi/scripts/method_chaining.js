// Assumptions: connected session, test schema exists on db, employee table exists

// New method chaining used for executing a SQL SELECT statement
// Available for both, XSession and NodeSession
// Recommended way for executing queries
var employees = db.getTable('employee');

var res = employees.select(['name', 'age'])
        .where('name like :param')
        .orderBy(['name'])
        .bind('param', 'm%').execute();

// Traditional SQL execution by passing an SQL string
// This is only available when using a low-level NodeSession
// It should only be used when absolutely necessary
var result = session.sql('SELECT name, age ' +
        'FROM employee ' +
        'WHERE name like ? ' +
        'ORDER BY name').bind('m%').execute();