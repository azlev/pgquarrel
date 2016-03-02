#include "schema.h"

/*
 * CREATE SCHEMA
 * DROP SCHEMA
 * ALTER SCHEMA
 * COMMENT ON SCHEMA
 *
 * TODO
 *
 * ALTER SCHEMA ... RENAME TO
 */

PQLSchema *
getSchemas(PGconn *c, int *n)
{
	PQLSchema	*s;
	PGresult	*res;
	int			i;

	logNoise("schema: server version: %d", PQserverVersion(c));

	res = PQexec(c,
				 "SELECT n.oid, nspname, obj_description(n.oid, 'pg_namespace') AS description, pg_get_userbyid(nspowner) AS nspowner, nspacl FROM pg_namespace n WHERE nspname !~ '^pg_' AND nspname <> 'information_schema' ORDER BY nspname");

	if (PQresultStatus(res) != PGRES_TUPLES_OK)
	{
		logError("query failed: %s", PQresultErrorMessage(res));
		PQclear(res);
		PQfinish(c);
		/* XXX leak another connection? */
		exit(EXIT_FAILURE);
	}

	*n = PQntuples(res);
	if (*n > 0)
		s = (PQLSchema *) malloc(*n * sizeof(PQLSchema));
	else
		s = NULL;

	logDebug("number of schemas in server: %d", *n);

	for (i = 0; i < *n; i++)
	{
		s[i].oid = strtoul(PQgetvalue(res, i, PQfnumber(res, "oid")), NULL, 10);
		s[i].schemaname = strdup(PQgetvalue(res, i, PQfnumber(res, "nspname")));
		if (PQgetisnull(res, i, PQfnumber(res, "description")))
			s[i].comment = NULL;
		else
			s[i].comment = strdup(PQgetvalue(res, i, PQfnumber(res, "description")));

		s[i].owner = strdup(PQgetvalue(res, i, PQfnumber(res, "nspowner")));
		if (PQgetisnull(res, i, PQfnumber(res, "nspacl")))
			s[i].acl = NULL;
		else
			s[i].acl = strdup(PQgetvalue(res, i, PQfnumber(res, "nspacl")));

		logDebug("schema %s", formatObjectIdentifier(s[i].schemaname));
	}

	PQclear(res);

	return s;
}

void
freeSchemas(PQLSchema *s, int n)
{
	if (n > 0)
	{
		int	i;

		for (i = 0; i < n; i++)
		{
			free(s[i].schemaname);
			if (s[i].comment)
				free(s[i].comment);
			if (s[i].acl)
				free(s[i].acl);
			free(s[i].owner);
		}

		free(s);
	}
}

void
dumpDropSchema(FILE *output, PQLSchema s)
{
	fprintf(output, "\n\n");
	fprintf(output, "DROP SCHEMA %s;",
			formatObjectIdentifier(s.schemaname));
}

void
dumpCreateSchema(FILE *output, PQLSchema s)
{
	fprintf(output, "\n\n");
	fprintf(output, "CREATE SCHEMA %s;",
			formatObjectIdentifier(s.schemaname));

	/* comment */
	if (options.comment && s.comment != NULL)
	{
		fprintf(output, "\n\n");
		fprintf(output, "COMMENT ON SCHEMA %s IS '%s';",
				formatObjectIdentifier(s.schemaname),
				s.comment);
	}

	/* owner */
	if (options.owner)
	{
		fprintf(output, "\n\n");
		fprintf(output, "ALTER SCHEMA %s OWNER TO %s;",
				formatObjectIdentifier(s.schemaname),
				s.owner);
	}

	/* privileges */
	/* XXX second s.obj isn't used. Add an invalid PQLObject? */
	if (options.privileges)
	{
		PQLObject tmp;

		/*
		 * don't use schemaname because objects without qualification use
		 * objectname as name.
		 * */
		tmp.schemaname = NULL;
		tmp.objectname = s.schemaname;

		dumpGrantAndRevoke(output, PGQ_SCHEMA, tmp, tmp, NULL, s.acl, NULL);
	}
}

void
dumpAlterSchema(FILE *output, PQLSchema a, PQLSchema b)
{
	if (strcmp(a.schemaname, b.schemaname) != 0)
	{
		fprintf(output, "\n\n");
		fprintf(output, "ALTER SCHEMA %s RENAME TO %s;",
				formatObjectIdentifier(a.schemaname),
				formatObjectIdentifier(b.schemaname));
	}

	/* comment */
	if (options.comment)
	{
		if ((a.comment == NULL && b.comment != NULL) ||
				(a.comment != NULL && b.comment != NULL &&
				 strcmp(a.comment, b.comment) != 0))
		{
			fprintf(output, "\n\n");
			fprintf(output, "COMMENT ON SCHEMA %s IS '%s';",
					formatObjectIdentifier(b.schemaname),
					b.comment);
		}
		else if (a.comment != NULL && b.comment == NULL)
		{
			fprintf(output, "\n\n");
			fprintf(output, "COMMENT ON SCHEMA %s IS NULL;",
					formatObjectIdentifier(b.schemaname));
		}
	}

	/* owner */
	if (options.owner)
	{
		if (strcmp(a.owner, b.owner) != 0)
		{
			fprintf(output, "\n\n");
			fprintf(output, "ALTER SCHEMA %s OWNER TO %s;",
					formatObjectIdentifier(b.schemaname),
					b.owner);
		}
	}

	/* privileges */
	if (options.privileges)
	{
		PQLObject tmpa, tmpb;

		/*
		 * don't use schemaname because objects without qualification use
		 * objectname as name.
		 * */
		tmpa.schemaname = NULL;
		tmpa.objectname = a.schemaname;
		tmpb.schemaname = NULL;
		tmpb.objectname = b.schemaname;

		if (a.acl != NULL || b.acl != NULL)
			dumpGrantAndRevoke(output, PGQ_SCHEMA, tmpa, tmpb, a.acl, b.acl, NULL);
	}
}
