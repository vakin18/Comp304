#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vedat Can Akin");
MODULE_DESCRIPTION("Linux module for traversing processes");
MODULE_VERSION("0.01");

int PID = -1;
char *traverseType = "-x";

module_param(PID, int, 0000);
MODULE_PARM_DESC(PID, "PID of a process");

module_param(traverseType, charp, 0000);
MODULE_PARM_DESC(traverseType, "Type of the traversal");

// void BFS(struct list_head *frontier);
void BFS(struct task_struct *task);
void DFS(struct task_struct *task);
void visitTask(struct task_struct *task);

static int __init my_module_init(void)
{
    printk(KERN_INFO "Module inserted!\n");
    if (!(PID < 0))
    {
        struct task_struct *task = get_pid_task(find_get_pid(PID), PIDTYPE_PID);
        if (task == NULL)
        {
            printk(KERN_INFO "Invalid PID!\n");
            return 0;
        }
        if (strcmp(traverseType, "-b") == 0)
        {
            BFS(task);
        }

        else if (strcmp(traverseType, "-d") == 0)
        {
            DFS(task);
        }
    }
    return 0;
}
static void __exit my_module_exit(void)
{
    printk(KERN_INFO "Module removed!\n");
}
void BFS(struct task_struct *task)
{
    printk(KERN_INFO "BFS is not implemented!\n");
}

void DFS(struct task_struct *task)
{
    struct task_struct *child;
    struct list_head *list;
    visitTask(task);

    if (!list_empty(&task->children))
    {
        list_for_each(list, &task->children)
        {
            child = list_entry(list, struct task_struct, sibling);
            DFS(child);
        }
    }
}

void visitTask(struct task_struct *task)
{
    printk(KERN_INFO "PID: %d, Name: %s\n", task->pid, task->comm);
}

/**
void BFS(struct list_head *frontier)
{
    struct task_struct *currentTask;
    struct list_head *child;
    struct list_head *nextFrontier;
    struct list_head *list;
    if (!list_empty(frontier))
    {
        list_for_each(list, frontier)
        {
            currentTask = list_entry(list, struct task_struct, sibling);
            if (currentTask == NULL)
            {
                printk(KERN_INFO "Something went wrong!\n");
                break;
            }
            if (currentTask->pid != 0)
            {
                visitTask(currentTask);
            }
        }

        list_for_each(list, frontier)
        {
            currentTask = list_entry(list, struct task_struct, sibling);
            if (!list_empty(&currentTask->children))
            {
                printk(KERN_INFO "It's not empty!\n");
                // list_for_each(child, &currentTask->children)
                // {
                // //     //     // if (nextFrontier == NULL)
                // //     //     // {
                // //     //     //     nextFrontier = child;
                // //     //     // }
                // //     //     // else
                // //     //     // {
                // //     //     //     list_add(child, nextFrontier);
                // //     //     // }
                // }
            }
            else
            {
                printk(KERN_INFO "It is empty!\n");
            }
        }

        // BFS(nextFrontier);
    } 
}
**/

module_init(my_module_init);
module_exit(my_module_exit);
