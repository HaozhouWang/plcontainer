-- first some tests of basic functionality
--
-- better succeed
--
select writeFile();
-- make sure the file doesn't exist on the host filesystem
\! ls -l /tmp/foo

select rlog100();
select pylog100();
select pylog(10000, 10);
select concat(fname, lname) from users;
select concatall();
select nested_call_three('a');
select nested_call_two('a');
select nested_call_one('a');
select invalid_function();
select invalid_syntax();