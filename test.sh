#!/usr/bin/env bash

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

usage() {
    echo -e "\n\nUsage: test.sh [arguments] \n"
    echo -e "test.sh - Script used to run a variety of tests to test httpserver executable"

    echo -e "Optional Arguments:\n"
    echo -e "\t-h, --help\t\t\t"
    echo -e "\t\tDisplay this help menu"

    echo -e "\tone"
    echo -e "\t\tRuns one test function"

    echo -e "\ttwo"
    echo -e "\t\tRuns two test function"

    echo -e "\tthree"
    echo -e "\t\tRuns three test function"

    echo -e "\t-a, --all"
    echo -e "\t\tRuns all test functions"
}

check_test() {
    RET_VAL=$?
    TEST=$1

    if [[ $RET_VAL -eq 0 ]];then
        echo -e "${TEST}\t\t${GREEN}PASS${NC}"
    else
        echo -e "${TEST}\t\t${RED}FAIL${NC}"
    fi
}
test_health(){
    curl http://localhost:8080/ --request-target /httpserver &
    curl http://localhost:8080/ --request-target /README &
    curl http://localhost:8080/ --request-target /README &
    curl http://localhost:8080/ --request-target /healthcheck &
    curl http://localhost:8080/ --request-target /README &

}
test_get() {
    time curl  --output g http://localhost:8080/ --request-target /testfile1 & 
    curl  --output g http://localhost:8080/ --request-target /testfile2 &
    curl --output g http://localhost:8080/ --request-target /testfile3 &
    curl --output g http://localhost:8080/ --request-target /testfile4 &
    curl --output g http://localhost:8080/ --request-target /testfile5 &
    curl --output g http://localhost:8080/ --request-target /testfile6 &
    curl --output g http://localhost:8080/ --request-target /testfile7 &
    curl --output g http://localhost:8080/ --request-target /testfile8 & 
    #check_test "test_one

    wait

    #check_test "test_one
}

test_put() {
    curl -v -T DESIGN.pdf http://localhost:8080/ --request-target /healthcheck
    #check_test "test_two"
}

test_head() {
    #curl -I localhost:8080/filename
    #curl --head localhost:8080/filename
    curl -I localhost:8080/testfile1
    #check_test "test_three"
}
test_multiput_1() {
    curl  -v -s http://localhost:8080/testfile &
    curl  -v -s http://localhost:8080/testfile1 &
    curl  -v -s http://localhost:8080/testfile2 &
   # curl -v -T testfile http://localhost:8080/testfile1 &
    #curl -v -T testfile http://localhost:8080/testfile1 &
    #curl -v -T testfile http://localhost:8080/testfile1 &
    #curl -v -T testfile http://localhost:8080/testfile1 &
    #curl -v -T testfile http://localhost:8080/testfile1 &
    #curl -v -T testfile http://localhost:8080/testfile1 &
    #curl -v -T testfile http://localhost:8080/testfile1 &


   # curl  -v -s http://localhost:8080/ --request-target /piss &
   # curl -v -T DESIGN.pdf http://localhost:8080/ --request-target /testfile2^ &
   # curl -v -T DESIGN.pdf http://localhost:8080/ --request-target /pezz &
   # curl -v -T DESIGN.pdf http://localhost:8080/ --request-target /testfile1 &
   # curl -v -T DESIGN.pdf http://localhost:8080/ --request-target /testfilecopy &
   # curl -I localhost:8080/tosend &
    wait
}

test_multiput_2() {
    #curl  -v -s http://localhost:8080/ --request-target /testfile &
    #curl  -v -s http://localhost:8080/ --request-target /testfile1 &
    #curl  -v -s http://localhost:8080/ --request-target /testfile2 &
    curl -v -T put_target http://localhost:8080/ --request-target /testfile1 &
    curl -v -T put_target http://localhost:8080/ --request-target /testfile1 &
    curl -v -T put_target http://localhost:8080/ --request-target /testfile1 &
    curl -v -T put_target http://localhost:8080/ --request-target /testfile1 &
    curl -v -T put_target http://localhost:8080/ --request-target /testfile1 &
    curl -v -T put_target http://localhost:8080/ --request-target /testfile1 &
    curl -v -T put_target http://localhost:8080/ --request-target /testfile1 &
    wait
}
test_multiput_3() {    
    curl -v -T DESIGN.pdf http://localhost:8080/ --request-target /testfile1 &
    curl -v -T DESIGN.pdf http://localhost:8080/ --request-target /testfile1 &
    curl -v -T DESIGN.pdf http://localhost:8080/ --request-target /testfile1 &
    curl -v -T DESIGN.pdf http://localhost:8080/ --request-target /testfile1 &
    curl -v -T DESIGN.pdf http://localhost:8080/ --request-target /testfile1 &
    curl -v -T DESIGN.pdf http://localhost:8080/ --request-target /testfile1 &
    curl -v -T DESIGN.pdf http://localhost:8080/ --request-target /testfile1 &
wait
}

    #curl  -v -s http://localhost:8080/ --request-target /piss &
    #curl -v -T DESIGN.pdf http://localhost:8080/ --request-target /testfile2^ &
    #curl -v -T DESIGN.pdf http://localhost:8080/ --request-target /pezz &
    #curl -v -T DESIGN.pdf http://localhost:8080/ --request-target /testfile1 &
    #curl -v -T DESIGN.pdf http://localhost:8080/ --request-target /testfilecopy &
    #curl -I localhost:8080/tosend &





while [ "$1" != "" ]; do
    case $1 in
        get)
            test_get
            ;;
        put)
            test_put
            ;;
        multiput_1)
            test_multiput_1
            ;;
        health)
            test_health
            ;;
        multiput_2)
            test_multiput_2
            ;;
        multiput_3)
            test_multiput_3
            ;;
        head)
            test_head
            ;;
        -a | --all)
            test_get
            test_put
            test_head
            ;;
        -h | --help)
            usage
            exit
            ;;
        *)
            usage
            exit 1
    esac
    shift
done
