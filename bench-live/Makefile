all: bench-async

bench-async: dhcp-client-async.c frame.c utils.c workers-async.c client-queue.c xid-tab.c lib/hash.c lib/cqueue.c
	gcc dhcp-client-async.c frame.c utils.c workers-async.c client-queue.c xid-tab.c lib/hash.c lib/cqueue.c -o bench-async -lpthread
 

bench: dhcp-client.c frame.c utils.c
	gcc dhcp-client.c frame.c utils.c -o bench

hash-test: lib/hash-test.c lib/hash.c
	gcc lib/hash-test.c lib/hash.c -o hash-test

cq-test: lib/cqueue-test.c lib/cqueue.c
	gcc lib/cqueue-test.c lib/cqueue.c -o cq-test

clean:
	rm -f *.o lib/*o bench bench-async hash-test cq-test
