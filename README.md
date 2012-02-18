###Introduction###
tdns is a threaded DNS lookup utility.

###Compilation###
```
    % make
```
The binary will be named `tdns`.

###Usage###
```
    % tdns INPUT_FILE [INPUT_FILE [...]] OUTPUT_FILE
```
The input files are text files with one domain per line. Blank lines are ignored.
tdns will then write the domain names and IP addresses associated with those domains to the output file.

###Example###
Input file:

```
    google.com
    yahoo.com
    usrsb.in
    github.com
```

Output:

```
    github.com, 207.97.227.239
    yahoo.com, 98.139.183.24
    google.com, 74.125.225.4
    usrsb.in, 205.185.117.40
```

###Credits###
`util.*` and `queue.*` provided by [Andy Sayler](https://github.com/asayler/CU-CS3753-2012-PA2).
