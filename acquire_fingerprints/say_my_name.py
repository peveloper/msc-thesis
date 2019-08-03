import subprocess
import sys

proc = subprocess.Popen(["python", "say_my_name.py"],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE)


proc.stdin.write("matthew\n")
proc.stdin.write("mark\n")
proc.stdin.write("luke\n")
proc.stdin.close()

while proc.returncode is None:
    proc.poll()

print("I got back from the program this:\n{0}".format(proc.stdout.read()))
