' sample.bas -- a gBASIC program for whisker's code editor to show.
'          ten spaces inside one comment, kept
' The colours are not whisker's: code.c reads gBASIC and hands whisker
' runs of "these characters in this colour". whisker knows no language.

program sample

' A table of the first few squares and cubes.
function powers(n)
    rows = []
    for i = 1 to n
        append(rows, [i, i * i, i * i * i])
    next i
    return rows
end function

' Words that are longer than some length, in the order given.
function longer(words, length)
    kept = []
    for each w in words
        if len(w) > length then
            append(kept, w)
        end if
    next
    return kept
end function

function check(label, expected, actual)
    if expected = actual then
        print "ok   " + label
        return true
    end if
    print "MISMATCH " + label + ": expected '" + string(expected) + "', got '" + string(actual) + "'"
    return false
end function

table = powers(5)
check("five rows", 5, len(table))
check("the cube of 4", 64, table[4][3])

animals = ["cat", "whisker", "robot", "ox"]
check("longer than three", 2, len(longer(animals, 3)))

total = 0
i = 0
while i < 10
    i += 1
    if i mod 2 = 0 then continue
    total += i
end while
check("odd numbers to ten", 25, total)

print "done"
end program
