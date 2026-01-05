all:
	# rm -vf dfs1/* dfs2/* dfs3/* dfs4/*
	gcc dfs.c send_receive.c -Wunused -o dfs
	gcc dfc.c send_receive.c -Wunused -lssl -lcrypto -o dfc

clean:
	rm -f dfs dfc

clean-dirs:
	rm -vf dfs1/* dfs2/* dfs3/* dfs4/*


.PHONY: all clean