#!/bin/sh
set -eu
awk -F, -v directory="$HYDRA_WORKFLOW_OUTPUTS_DIR" '
BEGIN { schedule=directory "/schedule.csv"; metrics=directory "/metrics.csv" }
NR == 1 { if ($0 != "id,arrival,duration") exit 2; next }
{ if (NF != 3 || $2 !~ /^[0-9]+$/ || $3 !~ /^[1-9][0-9]*$/ || seen[$1]++) exit 2
  id[++n]=$1; arrival[n]=$2+0; duration[n]=$3+0 }
function simulate(policy,    completed,time,i,pick,earliest,waiting,turnaround,sumwait,sumturn,worst,worstid,j,value,rank) {
    for(i=1;i<=n;i++) done[i]=0
    completed=0;time=0;sumwait=0;sumturn=0;worst=-1
    while(completed<n) {
        pick=0;earliest=-1
        for(i=1;i<=n;i++) if(!done[i]) {
            if(earliest<0 || arrival[i]<earliest) earliest=arrival[i]
            if(arrival[i]<=time && (!pick ||
                (policy=="FCFS" && arrival[i]<arrival[pick]) ||
                (policy=="SJF" && (duration[i]<duration[pick] || (duration[i]==duration[pick] && arrival[i]<arrival[pick]))))) pick=i
        }
        if(!pick) { time=earliest;continue }
        waiting=time-arrival[pick];turnaround=waiting+duration[pick]
        printf "%s,%s,%d,%d,%d,%d\n",policy,id[pick],time,time+duration[pick],waiting,turnaround >> schedule
        time+=duration[pick];done[pick]=1;turns[++completed]=turnaround
        sumwait+=waiting;sumturn+=turnaround
        if(waiting>worst) { worst=waiting;worstid=id[pick] }
    }
    for(i=2;i<=n;i++) { value=turns[i];j=i-1;while(j>=1 && turns[j]>value){turns[j+1]=turns[j];j--}turns[j+1]=value }
    rank=int(0.95*n);if(rank<0.95*n)rank++
    printf "%s,%d,%.6f,%.6f,%d,%d,%s\n",policy,n,sumwait/n,sumturn/n,turns[rank],worst,worstid >> metrics
}
END {
    if(n!=12)exit 2
    print "policy,id,start,finish,wait,turnaround" > schedule
    print "policy,n,mean_wait,mean_turnaround,p95_turnaround,max_wait,worst_wait_job" > metrics
    simulate("FCFS");simulate("SJF")
}' "$HYDRA_WORKFLOW_INPUTS_DIR/jobs"
{
    cat analyst.txt
    printf '\n\nMeasured metrics:\n'
    cat "$HYDRA_WORKFLOW_OUTPUTS_DIR/metrics.csv"
    printf '\nComplete schedule:\n'
    cat "$HYDRA_WORKFLOW_OUTPUTS_DIR/schedule.csv"
} > "$HYDRA_WORKFLOW_OUTPUTS_DIR/analyst-prompt"
